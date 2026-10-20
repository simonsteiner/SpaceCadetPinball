#include "pch.h"
#include "DefinitiveEdition.h"

#include "TableView.h"

namespace
{
	TableView Previous{};
	TableView Last{};
} // namespace

void DefinitiveEdition::Update(const TableView& view)
{
	Previous = Last;
	Last = view;
}

void DefinitiveEdition::Reset()
{
	Previous = TableView{};
	Last = TableView{};
}

const TableView& DefinitiveEdition::PreviousView()
{
	return Previous;
}
