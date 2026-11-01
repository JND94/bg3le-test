// Minimal adapters for the actual InputText methods extracted by the runner.
// Uses real ImGui state and rendering, without the game, SDL or Vulkan.
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <optional>
#include <string>
#include <sys/resource.h>

#ifdef NDEBUG
#error The regression test requires assertions
#endif

static std::string clipboard;

using STDString = std::string;
namespace lua { using ImguiHandle = int; }
namespace bg3se::extui {
struct DrawingContext { ImVec2 Scale(ImVec2 size) { return size; } };
ImVec2 ToImVec(ImVec2 size) { return size; }
struct Queue { template<class... Args> void Call(Args...) {} };
struct ManagerFixture { Queue queue; Queue& GetEventQueue() { return queue; } };
struct InputText {
    static constexpr std::size_t MaxSize = 16384;
    std::string Label = "External text";
    std::string Text = "previous text long enough to retain a selection";
    std::optional<std::string> Hint;
    int Flags = 0, Handle = 1;
    bool reloadText_ = false, OnChange = false;
    ManagerFixture* Manager = nullptr;
    ImVec2 SizeHint{};
    void StyledRender(DrawingContext&);
    void SetText(STDString);
};
} // namespace bg3se::extui

#include "input-text-under-test.inc"

int main(int argc, char** argv)
{
    // Baseline failures must not leave core dumps in a developer's checkout.
    rlimit limit{0, 0};
    if (setrlimit(RLIMIT_CORE, &limit) != 0)
        return 2;
    assert(argc == 2);
    const std::string scenario = argv[1];
    const bool readonly = scenario.find("readonly") != std::string::npos;
    const bool inherited = scenario.find("inherited") != std::string::npos;
    const bool other = scenario.find("other") != std::string::npos;
    const bool no_state = scenario.find("no-state") != std::string::npos;
    const bool no_reload = scenario.find("no-reload") != std::string::npos;
    const bool resize = scenario == "editable-resize";
    const bool selection = scenario.find("selection") != std::string::npos;
    const bool copy = scenario.find("utf8") != std::string::npos || scenario.find("nul") != std::string::npos;
    const bool becomes_readonly = scenario == "editable-to-readonly";
    const bool becomes_editable = scenario.find("readonly-to-editable") == 0;

    ImGuiContext* context = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigInputTrickleEventQueue = false;
    ImGui::GetPlatformIO().Platform_SetClipboardTextFn = [](ImGuiContext*, const char* text) { clipboard = text; };
    ImGui::GetPlatformIO().Platform_GetClipboardTextFn = [](ImGuiContext*) { return clipboard.c_str(); };
    io.DisplaySize = ImVec2(800, 600);
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    bg3se::extui::InputText field;
    if (readonly && !inherited && !becomes_readonly)
        field.Flags = ImGuiInputTextFlags_ReadOnly;
    bg3se::extui::DrawingContext drawing;
    ImVec2 click;
    ImGuiID id = 0;
    auto frame = [&](bool switch_focus) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(760, 400), ImGuiCond_Always);
        ImGui::Begin("InputText reload regression");
        id = ImGui::GetID(field.Label.c_str());
        if (switch_focus)
            ImGui::SetActiveID(ImGui::GetID("Other item"), ImGui::GetCurrentWindow());
        if (inherited)
            ImGui::PushItemFlag(ImGuiItemFlags_ReadOnly, true);
        field.StyledRender(drawing);
        if (inherited)
            ImGui::PopItemFlag();
        ImVec2 min = ImGui::GetItemRectMin();
        click = ImVec2(min.x + 8, min.y + ImGui::GetFrameHeight() * 0.5f);
        ImGui::End();
        ImGui::Render();
    };

    // Let ImGui itself create the retained input state, including the null
    // TextA buffer for read-only fields. Do not manufacture a broken state.
    frame(false);
    if (!no_state) {
        io.AddMousePosEvent(click.x, click.y);
        io.AddMouseButtonEvent(0, true);
        frame(false);
        assert(context->ActiveId == id);
        auto state = ImGui::GetInputTextState(id);
        assert(state != nullptr);
        assert(state->TextLen == static_cast<int>(field.Text.size()));
        if (readonly && !becomes_readonly) {
            assert(state->Flags & ImGuiInputTextFlags_ReadOnly);
            assert(state->TextA.Data == nullptr);
        } else {
            assert(state->TextA.Data != nullptr);
        }
        if (selection)
            state->SelectAll();
        io.AddMouseButtonEvent(0, false);
    }

    std::string expected = no_reload ? field.Text
        : scenario.find("empty") != std::string::npos ? "" : "updated external text";
    if (scenario.find("utf8") != std::string::npos)
        expected = "new \xc3\xa9 \xe4\xb8\xad";
    if (scenario.find("nul") != std::string::npos)
        expected = std::string("new\0hidden", 10);
    if (!no_reload)
        field.SetText(expected);
    if (becomes_readonly)
        field.Flags = ImGuiInputTextFlags_ReadOnly;
    if (becomes_editable)
        field.Flags = 0;
    if (resize)
        io.AddInputCharactersUTF8(std::string(200, 'z').c_str());
    frame(other);
    assert(!field.reloadText_);
    if (selection) {
        auto state = ImGui::GetInputTextState(id);
        assert(state);
        assert(state->GetSelectionStart() == 0);
        assert(state->TextLen == static_cast<int>(std::strlen(expected.c_str())));
        assert(state->GetSelectionEnd() == state->TextLen);
        assert(state->GetCursorPos() <= state->TextLen);
    }
    if (copy) {
        // Exercise copy after the changed text has been rendered and the
        // focused input's keyboard shortcut routing has been established.
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        io.AddKeyEvent(ImGuiKey_LeftCtrl, true);
        frame(false);
        io.AddKeyEvent(ImGuiKey_C, true);
        frame(false);
        assert(clipboard == std::string(expected.c_str()));
    }
    if (becomes_editable && other) {
        // Release the synthetic other item before clicking the input again.
        ImGui::ClearActiveID();
        io.AddMousePosEvent(click.x, click.y);
        io.AddMouseButtonEvent(0, true);
        frame(false);
        auto state = ImGui::GetInputTextState(id);
        assert(context->ActiveId == id && state && state->TextA.Data);
        assert(std::strcmp(state->TextA.Data, expected.c_str()) == 0);
    }
    if (resize) {
        assert(std::count(field.Text.begin(), field.Text.end(), 'z') == 200);
        assert(field.Text.size() >= 200);
    } else {
        assert(field.Text == expected);
    }
    if (!readonly && !no_state && !becomes_readonly) {
        auto state = ImGui::GetInputTextState(id);
        assert(state && state->TextA.Data);
        assert(state->TextLen == static_cast<int>(field.Text.size()));
        assert(std::strcmp(state->TextA.Data, field.Text.c_str()) == 0);
    }
    ImGui::DestroyContext(context);
    return 0;
}
