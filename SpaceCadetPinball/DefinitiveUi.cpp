#include "pch.h"
#include "DefinitiveUi.h"

#include "DefinitiveText.h"
#include "options.h"

float DefinitiveUi::LayoutScale = 1;
DefinitiveUi::Device DefinitiveUi::Last = DefinitiveUi::Device::Keyboard;
ImRect DefinitiveUi::Row{};
bool DefinitiveUi::RowFocused = false;
bool DefinitiveUi::FocusNext = false;

void DefinitiveUi::NewFrame()
{
	auto height = ImGui::GetMainViewport()->Size.y;
	LayoutScale = std::clamp(height / 720.0f, 0.6f, 4.0f) * options::Options.UIScale;

	// The device whose input came last decides the hints
	auto& io = ImGui::GetIO();
	if (io.MouseDelta.x != 0 || io.MouseDelta.y != 0 || ImGui::IsAnyMouseDown())
		Last = Device::Pointer;
	for (auto key = ImGuiKey_Keyboard_BEGIN; key < ImGuiKey_Keyboard_END; key = static_cast<ImGuiKey>(key + 1))
	{
		if (ImGui::IsKeyPressed(key, false))
			Last = Device::Keyboard;
	}
	for (auto key = ImGuiKey_Gamepad_BEGIN; key < ImGuiKey_Gamepad_END; key = static_cast<ImGuiKey>(key + 1))
	{
		if (ImGui::IsKeyPressed(key, false))
			Last = Device::Gamepad;
	}
}

float DefinitiveUi::Scale()
{
	return LayoutScale;
}

void DefinitiveUi::SetScale(float scale)
{
	LayoutScale = scale;
}

DefinitiveUi::Device DefinitiveUi::LastDevice()
{
	return Last;
}

float DefinitiveUi::FontSize(float size)
{
	const auto& style = ImGui::GetStyle();
	return size * LayoutScale / (style.FontScaleMain * style.FontScaleDpi);
}

ImVec2 DefinitiveUi::TextSize(ImFont* font, float size, const char* text)
{
	return font->CalcTextSizeA(size * LayoutScale, FLT_MAX, 0, DefinitiveText::Visual(text));
}

void DefinitiveUi::DrawText(ImDrawList* drawList, ImFont* font, float size, ImVec2 position, ImU32 color,
							const char* text, float width)
{
	auto visual = DefinitiveText::Visual(text);
	if (width > 0 && DefinitiveText::RightToLeft())
		position.x += width - font->CalcTextSizeA(size * LayoutScale, FLT_MAX, 0, visual).x;
	drawList->AddText(font, size * LayoutScale, position, color, visual);
}

bool DefinitiveUi::MenuRow(const char* id, const char* label, bool warning, const char* aside, bool enabled,
						   float trailing)
{
	auto scale = LayoutScale;
	ImVec2 size{ImGui::CalcItemWidth(), DefinitiveTheme::RowHeight * scale};
	ImGui::BeginDisabled(!enabled);
	// Invisible buttons stay out of keyboard and gamepad navigation unless asked
	auto chosen = ImGui::InvisibleButton(id, size, ImGuiButtonFlags_EnableNav);
	ImGui::EndDisabled();

	// The mouse and touch move the focus too, so only one lamp is ever lit
	auto& io = ImGui::GetIO();
	auto pointed = ImGui::IsItemHovered() && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0 || ImGui::IsMouseClicked(0));
	if (enabled && (pointed || FocusNext))
	{
		ImGui::SetFocusID(ImGui::GetItemID(), ImGui::GetCurrentWindow());
		// Else ImGui takes the first key only to show where the focus is, and Enter wouldn't choose
		ImGui::SetNavCursorVisible(true);
		FocusNext = false;
	}
	Row = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
	RowFocused = enabled && ImGui::IsItemFocused();

	auto drawList = ImGui::GetWindowDrawList();
	if (RowFocused)
	{
		drawList->AddRectFilled(Row.Min, Row.Max, DefinitiveTheme::Space700, DefinitiveTheme::Rounding * scale);
		drawList->AddLine(ImVec2{Row.Min.x + DefinitiveTheme::Rounding * scale, Row.Min.y + 0.5f},
						  ImVec2{Row.Max.x - DefinitiveTheme::Rounding * scale, Row.Min.y + 0.5f},
						  DefinitiveColor(0xEE, 0xF2, 0xFA, 0x2E));
	}
	auto padding = 16 * scale, lampRadius = 6 * scale;
	auto centreY = (Row.Min.y + Row.Max.y) / 2;
	auto lamp = !enabled      ? DefinitiveTheme::Lamp::Off
				: !RowFocused ? DefinitiveTheme::Lamp::Dark
				: warning     ? DefinitiveTheme::Lamp::Warning
							  : DefinitiveTheme::Lamp::Lit;
	DefinitiveTheme::DrawLamp(drawList, ImVec2{Row.Min.x + padding + lampRadius, centreY}, lampRadius, lamp);

	auto textSize = DefinitiveTheme::HeadingSize;
	auto textX = Row.Min.x + padding + lampRadius * 2 + padding;
	auto textRight = Row.Max.x - padding - trailing;
	if (aside)
	{
		auto asideSize = TextSize(DefinitiveTheme::TextFont(), DefinitiveTheme::BodySize, aside);
		textRight -= asideSize.x + padding;
		DrawText(drawList, DefinitiveTheme::TextFont(), DefinitiveTheme::BodySize,
				 ImVec2{Row.Max.x - padding - asideSize.x, centreY - asideSize.y / 2}, DefinitiveTheme::Chrome400,
				 aside);
	}
	auto color = !enabled     ? DefinitiveTheme::Chrome600
				 : RowFocused ? DefinitiveTheme::Chrome50
							  : DefinitiveTheme::Chrome200;
	auto labelHeight = TextSize(DefinitiveTheme::TextFont(), textSize, label).y;
	DrawText(drawList, DefinitiveTheme::TextFont(), textSize, ImVec2{textX, centreY - labelHeight / 2}, color, label,
			 textRight - textX);
	return chosen && enabled;
}

ImRect DefinitiveUi::LastRow()
{
	return Row;
}

bool DefinitiveUi::LastRowFocused()
{
	return RowFocused;
}

void DefinitiveUi::FocusNextRow()
{
	FocusNext = true;
}

namespace
{
	// A key cap with a label, or with an arrow pointing in direction when the label is null
	float KeyCap(ImDrawList* drawList, ImVec2 position, float scale, const char* label,
				 ImGuiDir direction = ImGuiDir_None)
	{
		auto height = 26 * scale, padding = 7 * scale;
		auto font = DefinitiveTheme::HeadingFont();
		auto fontSize = 13 * scale;
		auto width =
			label ? std::max(height, font->CalcTextSizeA(fontSize, FLT_MAX, 0, label).x + padding * 2) : height;
		ImVec2 max{position.x + width, position.y + height};
		drawList->AddRectFilled(position, max, DefinitiveTheme::Space700, 4 * scale);
		drawList->AddRect(position, max, DefinitiveColor(0xEE, 0xF2, 0xFA, 0x50), 4 * scale);
		ImVec2 centre{position.x + width / 2, position.y + height / 2};
		if (label)
		{
			auto size = font->CalcTextSizeA(fontSize, FLT_MAX, 0, label);
			drawList->AddText(font, fontSize, ImVec2{centre.x - size.x / 2, centre.y - size.y / 2},
							  DefinitiveTheme::Chrome50, label);
		}
		else
		{
			ImGui::RenderArrow(drawList, ImVec2{centre.x - fontSize / 2, centre.y - fontSize / 2},
							   DefinitiveTheme::Chrome50, direction, 1.0f * scale * 13 / ImGui::GetFontSize());
		}
		return width;
	}

	// The four face buttons, with the one to press lit; ImGui's south is A on Xbox pads and the cross on PlayStation
	float FaceButtons(ImDrawList* drawList, ImVec2 position, float scale, ImGuiKey lit)
	{
		auto size = 26 * scale, radius = 4.5f * scale;
		ImVec2 centre{position.x + size / 2, position.y + size / 2};
		auto offset = size / 2 - radius;
		std::pair<ImGuiKey, ImVec2> buttons[] = {
			{ImGuiKey_GamepadFaceDown, ImVec2{0, offset}},
			{ImGuiKey_GamepadFaceRight, ImVec2{offset, 0}},
			{ImGuiKey_GamepadFaceUp, ImVec2{0, -offset}},
			{ImGuiKey_GamepadFaceLeft, ImVec2{-offset, 0}},
		};
		for (auto& [key, place] : buttons)
		{
			ImVec2 point{centre.x + place.x, centre.y + place.y};
			if (key == lit)
				drawList->AddCircleFilled(point, radius, DefinitiveTheme::Chrome50);
			else
				drawList->AddCircle(point, radius - 0.5f, DefinitiveTheme::Chrome400, 0, 1);
		}
		return size;
	}

	// A d-pad, with the arms to press lit
	float DPad(ImDrawList* drawList, ImVec2 position, float scale, bool vertical)
	{
		auto size = 26 * scale, arm = size / 3;
		auto x = position.x, y = position.y;
		auto verticalColor = vertical ? DefinitiveTheme::Chrome50 : DefinitiveTheme::Chrome600;
		auto horizontalColor = vertical ? DefinitiveTheme::Chrome600 : DefinitiveTheme::Chrome50;
		drawList->AddRectFilled(ImVec2{x + arm, y}, ImVec2{x + arm * 2, y + arm}, verticalColor, 2 * scale);
		drawList->AddRectFilled(ImVec2{x + arm, y + arm * 2}, ImVec2{x + arm * 2, y + size}, verticalColor, 2 * scale);
		drawList->AddRectFilled(ImVec2{x, y + arm}, ImVec2{x + arm, y + arm * 2}, horizontalColor, 2 * scale);
		drawList->AddRectFilled(ImVec2{x + arm * 2, y + arm}, ImVec2{x + size, y + arm * 2}, horizontalColor,
								2 * scale);
		drawList->AddRectFilled(ImVec2{x + arm, y + arm}, ImVec2{x + arm * 2, y + arm * 2}, DefinitiveTheme::Chrome600);
		return size;
	}
} // namespace

void DefinitiveUi::DrawHints(ImDrawList* drawList, ImVec2 position, std::initializer_list<Hint> hints)
{
	if (Last == Device::Pointer)
		return;
	auto scale = LayoutScale;
	auto gap = 6 * scale;
	auto x = position.x;
	for (auto& hint : hints)
	{
		if (Last == Device::Keyboard)
		{
			switch (hint.Does)
			{
			case Action::Choose:
				x += KeyCap(drawList, ImVec2{x, position.y}, scale, nullptr, ImGuiDir_Up) + gap;
				x += KeyCap(drawList, ImVec2{x, position.y}, scale, nullptr, ImGuiDir_Down);
				break;
			case Action::Select:
				x += KeyCap(drawList, ImVec2{x, position.y}, scale, "Enter");
				break;
			case Action::Back:
				x += KeyCap(drawList, ImVec2{x, position.y}, scale, "Esc");
				break;
			case Action::Change:
				x += KeyCap(drawList, ImVec2{x, position.y}, scale, nullptr, ImGuiDir_Left) + gap;
				x += KeyCap(drawList, ImVec2{x, position.y}, scale, nullptr, ImGuiDir_Right);
				break;
			case Action::Group:
				x += KeyCap(drawList, ImVec2{x, position.y}, scale, "Q") + gap;
				x += KeyCap(drawList, ImVec2{x, position.y}, scale, "E");
				break;
			}
		}
		else
		{
			switch (hint.Does)
			{
			case Action::Choose:
				x += DPad(drawList, ImVec2{x, position.y}, scale, true);
				break;
			case Action::Select:
				x += FaceButtons(drawList, ImVec2{x, position.y}, scale, ImGuiKey_GamepadFaceDown);
				break;
			case Action::Back:
				x += FaceButtons(drawList, ImVec2{x, position.y}, scale, ImGuiKey_GamepadFaceRight);
				break;
			case Action::Change:
				x += DPad(drawList, ImVec2{x, position.y}, scale, false);
				break;
			case Action::Group:
				x += KeyCap(drawList, ImVec2{x, position.y}, scale, "L") + gap;
				x += KeyCap(drawList, ImVec2{x, position.y}, scale, "R");
				break;
			}
		}
		x += gap * 1.5f;
		auto size = TextSize(DefinitiveTheme::TextFont(), DefinitiveTheme::LabelSize, hint.Label);
		DrawText(drawList, DefinitiveTheme::TextFont(), DefinitiveTheme::LabelSize,
				 ImVec2{x, position.y + 13 * scale - size.y / 2}, DefinitiveTheme::Chrome400, hint.Label);
		x += size.x + 24 * scale;
	}
}
