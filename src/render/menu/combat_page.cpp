#include <stdafx.hpp>
#include <render/menu/internal.hpp>
#include <render/menu/combat_widgets.hpp>

using namespace render::menu::detail;
using namespace render::menu::combat;

void menu_t::begin_combat_page(bool triggerbot)
{
    static constexpr const char *groups[]{"Global", "Pistol", "SMG", "Rifle", "Shotgun", "Sniper", "Heavy"};
    ImGui::SetCursorPos({24.0f, 24.0f});
    for (int i = 0; i < 7; ++i)
    {
        if (i)
            ImGui::SameLine(0.0f, 10.0f);
        if (tab_button(groups[i], this->m_weapon_group == i - 1) && this->m_weapon_group != i - 1)
        {
            this->m_weapon_group = i - 1;
            this->reset_content_animation();
        }
    }

    ImGui::SetCursorPos({14.0f, 68.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    ImGui::BeginChild(triggerbot ? "##trigger_cards" : "##aim_cards", {682.0f, 568.0f}, false);
    begin_cards(triggerbot ? "##trigger_grid" : "##aim_grid");
}

void menu_t::end_combat_page()
{
    end_cards();
    ImGui::EndChild();
    ImGui::PopStyleVar();
}
