#include "pch.h"
#include "font_selection.h"

#include "options.h"
#include "pb.h"
#include "score.h"
#include "winmain.h"
#include "translations.h"

static const char* popupName = "Font Selection";
bool font_selection::ShowDialogFlag = false;
char font_selection::DialogInputBuffer[512];

void font_selection::ShowDialog()
{
	ShowDialogFlag = true;
}

void font_selection::RenderDialog()
{
	if (ShowDialogFlag == true)
	{
		snprintf(DialogInputBuffer, sizeof(DialogInputBuffer), "%s", options::Options.FontFileName.V.c_str());
		ShowDialogFlag = false;
		if (!ImGui::IsPopupOpen(popupName))
		{
			ImGui::OpenPopup(popupName);
		}
	}

	bool unused_open = true;
	if (ImGui::BeginPopupModal(popupName, &unused_open, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("Font file to use: ");
		ImGui::SameLine();
		ImGui::InputText("##Font", DialogInputBuffer, IM_ARRAYSIZE(DialogInputBuffer));

		if (ImGui::Button(pb::get_rc_string(Msg::GenericOk)))
		{
			options::Options.FontFileName.V = DialogInputBuffer;
			ImGui::CloseCurrentPopup();
			winmain::Restart();
		}

		ImGui::SameLine();
		if (ImGui::Button(pb::get_rc_string(Msg::GenericCancel)))
			ImGui::CloseCurrentPopup();

		ImGui::EndPopup();
	}
}
