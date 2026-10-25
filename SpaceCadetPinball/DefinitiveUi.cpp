#include "pch.h"
#include "DefinitiveUi.h"

#include "DefinitiveText.h"
#include "options.h"

float DefinitiveUi::LayoutScale = 1;
DefinitiveUi::Device DefinitiveUi::Last = DefinitiveUi::Device::Keyboard;
ImRect DefinitiveUi::Row{};
bool DefinitiveUi::RowFocused = false;
bool DefinitiveUi::FocusNext = false;
const char* DefinitiveUi::Note = nullptr;
ImVec2 DefinitiveUi::Footer{};

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

std::string DefinitiveUi::FormatScore(int score, size_t places)
{
	auto digits = std::to_string(std::max(score, 0));
	std::string grouped;
	for (size_t i = 0; i < digits.size(); i++)
	{
		if (i > 0 && (digits.size() - i) % 3 == 0)
			grouped += ',';
		grouped += digits[i];
	}
	// DSEG's '!' is a blank as wide as a digit, which shows only its unlit segments
	while (grouped.size() < places)
		grouped.insert(grouped.begin(), '!');
	return grouped;
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

float DefinitiveUi::DrawWrapped(ImDrawList* drawList, ImFont* font, float size, ImVec2 position, ImU32 color,
								const char* text, float width)
{
	// Lines break in reading order and only then each goes into drawing order, so a right-to-left paragraph starts on
	// its first line too
	auto fontSize = size * LayoutScale;
	auto rightToLeft = DefinitiveText::RightToLeft();
	auto lines = 0;
	auto drawLine = [&](std::string_view line)
	{
		ImVec2 at{position.x, position.y + static_cast<float>(lines++) * fontSize};
		if (!drawList)
			return;
		auto visual = DefinitiveText::Shape(line);
		if (rightToLeft)
			at.x += width - font->CalcTextSizeA(fontSize, FLT_MAX, 0, visual.c_str()).x;
		drawList->AddText(font, fontSize, at, color, visual.c_str());
	};
	std::string_view rest{text};
	while (true)
	{
		auto newline = rest.find('\n');
		auto paragraph = rest.substr(0, newline);
		// Words join the line while it fits as drawn; a word wider than the line gets one of its own
		size_t lineStart = 0, lineEnd = 0, wordStart = 0;
		while (true)
		{
			auto space = paragraph.find(' ', wordStart);
			auto wordEnd = space == std::string_view::npos ? paragraph.size() : space;
			auto candidate = DefinitiveText::Shape(paragraph.substr(lineStart, wordEnd - lineStart));
			if (lineEnd > lineStart && font->CalcTextSizeA(fontSize, FLT_MAX, 0, candidate.c_str()).x > width)
			{
				drawLine(paragraph.substr(lineStart, lineEnd - lineStart));
				lineStart = lineEnd = lineEnd + 1;
				continue;
			}
			lineEnd = wordEnd;
			if (space == std::string_view::npos)
				break;
			wordStart = space + 1;
		}
		drawLine(paragraph.substr(lineStart));
		if (newline == std::string_view::npos)
			break;
		rest.remove_prefix(newline + 1);
	}
	return static_cast<float>(lines) * fontSize;
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
	auto labelY = centreY - labelHeight / 2;
	if (Note)
	{
		// The label moves up to make room for the note under it
		auto noteSize = TextSize(DefinitiveTheme::TextFont(), DefinitiveTheme::LabelSize - 1, Note);
		labelY -= noteSize.y / 2;
		// Cut at the controls on its right too
		drawList->PushClipRect(ImVec2{textX, Row.Min.y}, ImVec2{textRight, Row.Max.y}, true);
		DrawText(drawList, DefinitiveTheme::TextFont(), DefinitiveTheme::LabelSize - 1,
				 ImVec2{textX, labelY + labelHeight}, DefinitiveTheme::Chrome400, Note, textRight - textX);
		drawList->PopClipRect();
		Note = nullptr;
	}
	// A label too long for its room is cut at the controls on its right
	drawList->PushClipRect(ImVec2{textX, Row.Min.y}, ImVec2{textRight, Row.Max.y}, true);
	DrawText(drawList, DefinitiveTheme::TextFont(), textSize, ImVec2{textX, labelY}, color, label, textRight - textX);
	drawList->PopClipRect();
	return chosen && enabled;
}

int DefinitiveUi::Tabs(ImDrawList* drawList, ImVec2 position, float width, const std::vector<const char*>& labels,
					   int current)
{
	auto scale = LayoutScale;
	auto count = static_cast<int>(labels.size());
	auto index = current;
	// Not while a dialog over the screen, such as the font file's, has the keyboard
	auto focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
	if (focused && (ImGui::IsKeyPressed(ImGuiKey_Q, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false)))
		index = (index + count - 1) % count;
	if (focused && (ImGui::IsKeyPressed(ImGuiKey_E, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false)))
		index = (index + 1) % count;
	auto x = position.x;
	auto tabHeight = TabHeight * scale;
	auto tabFontSize = DefinitiveTheme::LabelSize + 1;
	std::vector<float> widths;
	auto total = 0.0f;
	for (auto label : labels)
	{
		widths.push_back(TextSize(DefinitiveTheme::HeadingFont(), tabFontSize, label).x + 20 * scale);
		total += widths.back();
	}
	// Narrow windows squeeze the tabs together
	auto squeeze = std::min(1.0f, width / total);
	drawList->AddLine(ImVec2{x, position.y + tabHeight}, ImVec2{x + width, position.y + tabHeight},
					  DefinitiveTheme::Chrome600);
	for (auto tab = 0; tab < count; tab++)
	{
		auto tabWidth = widths[static_cast<size_t>(tab)] * squeeze;
		ImGui::SetCursorScreenPos(ImVec2{x, position.y});
		ImGui::PushID(tab);
		if (ImGui::InvisibleButton("tab", ImVec2{tabWidth, tabHeight}))
			index = tab;
		ImGui::PopID();
		auto selected = tab == current;
		auto font = selected ? DefinitiveTheme::HeadingFont() : DefinitiveTheme::TextFont();
		auto label = labels[static_cast<size_t>(tab)];
		auto labelSize = TextSize(font, tabFontSize, label);
		DrawText(drawList, font, tabFontSize,
				 ImVec2{x + (tabWidth - labelSize.x) / 2, position.y + (tabHeight - labelSize.y) / 2},
				 selected ? DefinitiveTheme::Chrome50 : DefinitiveTheme::Chrome400, label);
		if (selected)
			drawList->AddRectFilled(ImVec2{x + 8 * scale, position.y + tabHeight - 2 * scale},
									ImVec2{x + tabWidth - 8 * scale, position.y + tabHeight},
									DefinitiveTheme::LedOrange);
		x += tabWidth;
	}
	return index;
}

void DefinitiveUi::ParagraphRow(const char* id, const char* title, const char* aside, const char* text, const char* tag,
								float footer)
{
	auto scale = LayoutScale;
	auto width = ImGui::CalcItemWidth();
	auto padding = 16 * scale, lampRadius = 6 * scale;
	auto textX = padding + lampRadius * 2 + padding;
	auto titleHeight = DefinitiveTheme::BodySize * 1.3f * scale;
	auto paragraphWidth = width - textX - padding;
	auto paragraphHeight = DrawWrapped(nullptr, DefinitiveTheme::TextFont(), DefinitiveTheme::LabelSize + 1, ImVec2{},
									   0, text, paragraphWidth);
	auto footerHeight = footer > 0 ? (footer + 6) * scale : 0;
	ImVec2 size{width, padding * 0.75f + titleHeight + 4 * scale + paragraphHeight + footerHeight + padding * 0.75f};
	ImGui::InvisibleButton(id, size, ImGuiButtonFlags_EnableNav);

	// The mouse and touch move the focus too, as on a menu row
	auto& io = ImGui::GetIO();
	auto pointed = ImGui::IsItemHovered() && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0 || ImGui::IsMouseClicked(0));
	if (pointed || FocusNext)
	{
		ImGui::SetFocusID(ImGui::GetItemID(), ImGui::GetCurrentWindow());
		ImGui::SetNavCursorVisible(true);
		FocusNext = false;
	}
	Row = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
	RowFocused = ImGui::IsItemFocused();

	auto drawList = ImGui::GetWindowDrawList();
	if (RowFocused)
	{
		drawList->AddRectFilled(Row.Min, Row.Max, DefinitiveTheme::Space700, DefinitiveTheme::Rounding * scale);
		drawList->AddLine(ImVec2{Row.Min.x + DefinitiveTheme::Rounding * scale, Row.Min.y + 0.5f},
						  ImVec2{Row.Max.x - DefinitiveTheme::Rounding * scale, Row.Min.y + 0.5f},
						  DefinitiveColor(0xEE, 0xF2, 0xFA, 0x2E));
	}
	auto top = Row.Min.y + padding * 0.75f;
	DefinitiveTheme::DrawLamp(drawList, ImVec2{Row.Min.x + padding + lampRadius, top + titleHeight / 2}, lampRadius,
							  RowFocused ? DefinitiveTheme::Lamp::Lit : DefinitiveTheme::Lamp::Dark);
	auto titleRight = Row.Max.x - padding;
	for (auto [label, color] :
		 {std::pair{aside, DefinitiveTheme::Chrome400}, std::pair{tag, DefinitiveTheme::LedOrange}})
	{
		if (!label)
			continue;
		auto labelSize = TextSize(DefinitiveTheme::TextFont(), DefinitiveTheme::LabelSize, label);
		titleRight -= labelSize.x;
		DrawText(drawList, DefinitiveTheme::TextFont(), DefinitiveTheme::LabelSize,
				 ImVec2{titleRight, top + (titleHeight - labelSize.y) / 2}, color, label);
		titleRight -= padding;
	}
	DrawText(drawList, DefinitiveTheme::HeadingFont(), DefinitiveTheme::BodySize, ImVec2{Row.Min.x + textX, top},
			 RowFocused ? DefinitiveTheme::Chrome50 : DefinitiveTheme::Chrome200, title,
			 titleRight - Row.Min.x - textX);
	DrawWrapped(drawList, DefinitiveTheme::TextFont(), DefinitiveTheme::LabelSize + 1,
				ImVec2{Row.Min.x + textX, top + titleHeight + 4 * scale}, DefinitiveTheme::Chrome200, text,
				paragraphWidth);
	Footer = {Row.Min.x + textX, Row.Max.y - padding * 0.75f - footerHeight + 6 * scale};
}

ImVec2 DefinitiveUi::FooterTop()
{
	return Footer;
}

void DefinitiveUi::NextRowNote(const char* note)
{
	Note = note;
}

int DefinitiveUi::RowStep()
{
	if (!RowFocused)
		return 0;
	if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft))
		return -1;
	if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight))
		return 1;
	return 0;
}

bool DefinitiveUi::ToggleRow(const char* id, const char* label, bool on, bool enabled)
{
	auto scale = LayoutScale;
	ImVec2 size{52 * scale, 28 * scale};
	auto chosen = MenuRow(id, label, false, nullptr, enabled, size.x + 8 * scale);
	auto step = RowStep();
	auto drawList = ImGui::GetWindowDrawList();
	ImVec2 min{Row.Max.x - 16 * scale - size.x, (Row.Min.y + Row.Max.y - size.y) / 2};
	ImVec2 max{min.x + size.x, min.y + size.y};
	auto radius = size.y / 2;
	auto knob = size.y / 2 - 4 * scale;
	if (on && enabled)
	{
		drawList->AddRectFilled(min, max, DefinitiveColor(0xFF, 0x8C, 0x1A, 0x2E), radius);
		drawList->AddRect(min, max, DefinitiveTheme::LedOrange, radius);
		DefinitiveTheme::DrawLamp(drawList, ImVec2{max.x - radius, min.y + radius}, knob, DefinitiveTheme::Lamp::Lit);
	}
	else
	{
		drawList->AddRectFilled(min, max, DefinitiveTheme::Space950, radius);
		drawList->AddRect(min, max, DefinitiveTheme::Chrome600, radius);
		drawList->AddCircleFilled(ImVec2{min.x + radius, min.y + radius}, knob,
								  enabled ? DefinitiveTheme::Chrome200 : DefinitiveTheme::Chrome600);
	}
	return enabled && (chosen || (step < 0 && on) || (step > 0 && !on));
}

int DefinitiveUi::ChoiceRow(const char* id, const char* label, std::initializer_list<const char*> choices, int current,
							bool enabled)
{
	auto scale = LayoutScale;
	auto font = DefinitiveTheme::TextFont();
	auto padding = 12 * scale, height = 34 * scale;
	std::vector<float> widths;
	auto total = 6 * scale;
	for (auto choice : choices)
	{
		widths.push_back(std::max(36 * scale, TextSize(font, DefinitiveTheme::BodySize - 1, choice).x + padding * 2));
		total += widths.back();
	}
	auto chosen = MenuRow(id, label, false, nullptr, enabled, total + 8 * scale);
	auto count = static_cast<int>(choices.size());
	auto result = current;
	auto step = RowStep();
	if (enabled && step)
		result = std::clamp(current + step, 0, count - 1);

	auto drawList = ImGui::GetWindowDrawList();
	ImVec2 min{Row.Max.x - 16 * scale - total, (Row.Min.y + Row.Max.y - height) / 2};
	ImVec2 max{min.x + total, min.y + height};
	drawList->AddRectFilled(min, max, DefinitiveTheme::Space950, 6 * scale);
	drawList->AddRect(min, max, DefinitiveTheme::Chrome600, 6 * scale);
	auto x = min.x + 3 * scale;
	auto mouse = ImGui::GetIO().MousePos;
	auto tapped = -1;
	auto index = 0;
	for (auto choice : choices)
	{
		auto width = widths[static_cast<size_t>(index)];
		ImVec2 segmentMin{x, min.y + 3 * scale}, segmentMax{x + width, max.y - 3 * scale};
		if (mouse.x >= segmentMin.x && mouse.x < segmentMax.x && mouse.y >= segmentMin.y && mouse.y < segmentMax.y)
			tapped = index;
		if (index == current)
			drawList->AddRectFilled(segmentMin, segmentMax,
									enabled ? DefinitiveTheme::Space600 : DefinitiveTheme::Space800, 4 * scale);
		auto textSize = TextSize(font, DefinitiveTheme::BodySize - 1, choice);
		DrawText(drawList, font, DefinitiveTheme::BodySize - 1,
				 ImVec2{x + (width - textSize.x) / 2, (segmentMin.y + segmentMax.y - textSize.y) / 2},
				 index == current && enabled ? DefinitiveTheme::Chrome50 : DefinitiveTheme::Chrome400, choice);
		x += width;
		index++;
	}
	if (enabled && chosen)
		result = tapped >= 0 && ImGui::IsMouseReleased(0) ? tapped : (current + 1) % count;
	return result;
}

int DefinitiveUi::SliderRow(const char* id, const char* label, int value, int min, int max, int step,
							const char* format)
{
	auto scale = LayoutScale;
	auto trackWidth = 180 * scale, valueWidth = 56 * scale;
	MenuRow(id, label, false, nullptr, true, trackWidth + valueWidth + 16 * scale);
	auto result = std::clamp(value + RowStep() * step, min, max);

	auto drawList = ImGui::GetWindowDrawList();
	auto centreY = (Row.Min.y + Row.Max.y) / 2;
	ImVec2 trackMin{Row.Max.x - 16 * scale - trackWidth, centreY - 3 * scale};
	ImVec2 trackMax{trackMin.x + trackWidth, centreY + 3 * scale};
	// A press on the row near the track, and a drag from there, moves the knob to the pointer; Enter or the pad's A
	// make the row active too, but leave the knob where it is
	auto& io = ImGui::GetIO();
	if (ImGui::IsItemActive() && GImGui->ActiveIdSource == ImGuiInputSource_Mouse &&
		io.MouseClickedPos[0].x >= trackMin.x - 12 * scale)
	{
		auto fraction = std::clamp((io.MousePos.x - trackMin.x) / trackWidth, 0.0f, 1.0f);
		auto steps = std::lround(fraction * static_cast<float>(max - min) / static_cast<float>(step));
		result = std::clamp(min + static_cast<int>(steps) * step, min, max);
	}
	auto fraction = static_cast<float>(result - min) / static_cast<float>(std::max(max - min, 1));
	drawList->AddRectFilled(trackMin, trackMax, DefinitiveTheme::Space950, 3 * scale);
	drawList->AddRect(trackMin, trackMax, DefinitiveTheme::Chrome600, 3 * scale);
	ImVec2 knob{trackMin.x + trackWidth * fraction, centreY};
	drawList->AddRectFilled(trackMin, ImVec2{knob.x, trackMax.y}, DefinitiveTheme::LedOrange, 3 * scale);
	drawList->AddCircleFilled(knob, 10 * scale, DefinitiveTheme::Chrome200);
	drawList->AddCircle(knob, 10 * scale, DefinitiveTheme::Chrome50, 0, 1);

	char text[32];
	snprintf(text, sizeof text, format, result);
	auto textSize = TextSize(DefinitiveTheme::TextFont(), DefinitiveTheme::BodySize - 1, text);
	DrawText(drawList, DefinitiveTheme::TextFont(), DefinitiveTheme::BodySize - 1,
			 ImVec2{trackMin.x - 12 * scale - textSize.x, centreY - textSize.y / 2}, DefinitiveTheme::Chrome200, text);
	return result;
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

bool DefinitiveUi::TakeFocusRequest()
{
	auto requested = FocusNext;
	FocusNext = false;
	return requested;
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
			case Action::Erase:
				x += KeyCap(drawList, ImVec2{x, position.y}, scale, "Backspace");
				break;
			case Action::Finish:
				x += KeyCap(drawList, ImVec2{x, position.y}, scale, "Enter");
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
			case Action::Erase:
				x += FaceButtons(drawList, ImVec2{x, position.y}, scale, ImGuiKey_GamepadFaceRight);
				break;
			case Action::Finish:
				x += KeyCap(drawList, ImVec2{x, position.y}, scale, "Start");
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
