#include "pch.h"
#include "ProjOperation.h"
#include "CoordinateTransform.h"

IReadOnlyList<ProjOperation^>^ CoordinateTransform::ProjOperations()
{
	return gcnew ProjOperationList(this);
}

ProjOperationList::ProjOperationList(CoordinateTransform^ transform)
{
	if (!transform)
		throw gcnew ArgumentNullException("transform");

	m_transform = transform;
}

void ProjOperationList::Ensure()
{
	if (m_items)
		return;

	PJ_PROJ_INFO info = proj_pj_info(m_transform);

	const char* def = info.definition;
	List<String^>^ tokens = gcnew List<String^>();

	while (*def)
	{
		while (*def && Char::IsWhiteSpace(*def))
			def++;

		if (!*def)
			break;

		const char* start = def;
		bool in_str = false;
		bool found_str = false;

		while (*def && (in_str || !Char::IsWhiteSpace(*def)))
		{
			if (*def == '\"')
			{
				in_str = !in_str;
				found_str = true;
			}

			*def++;
		}

		String^ v = Utf8_PtrToString(start, def - start);

		if (found_str)
			v = v->Replace(L"\"\"", L"\uFFFD")->Replace(L"\"", L"")->Replace(L"\uFFFD", L"\"");

		tokens->Add(v);
	}

	auto tokenArray = tokens->ToArray();
	m_tokens = Array::AsReadOnly(tokenArray);

	if (tokenArray->Length && tokenArray[0] == "proj=pipeline")
	{
		int iStep = Array::IndexOf(tokenArray, "step");
		m_iCommonStart = 1;
		m_iCommonCount = iStep - 1;

		List<ProjOperation^>^ steps = gcnew List<ProjOperation^>();
		int iNext;
		do
		{
			iNext = Array::IndexOf(tokenArray, "step", iStep + 1);

			if (iNext < 0)
				iNext = tokenArray->Length;

			steps->Add(gcnew ProjOperation(this, iStep + 1, iNext - iStep - 1));
			iStep = iNext;
		} while (iNext < tokenArray->Length);

		m_items = steps->ToArray();
	}
	else
	{
		m_items = gcnew array<ProjOperation^> { gcnew ProjOperation(this, 0, m_tokens->Count)};
	}
}

ProjOperationDefinition::ProjOperationDefinitionCollection^ ProjOperationDefinition::LoadFuncs()
{
	auto ops = proj_list_operations();

	auto opList = gcnew List<ProjOperationDefinition^>();

	while (ops && ops->id)
	{
		const char* desc = *ops->descr;
		const char* nl = strchr(desc, '\n');

		String^ title, ^ details;
		ProjOperationType type = (ProjOperationType)-1;

		if (nl)
		{
			title = Utf8_PtrToString(desc, (ptrdiff_t)nl - (ptrdiff_t)desc);

			if (nl[1] == 0 || (nl[1] == '\t' && nl[2] == 0))
			{
				details = nullptr;
				type = ProjOperationType::Transformation;
			}
			else
			{
				details = Utf8_PtrToString(nl + 1);
				type = ProjOperationType::Projection;
			}
		}
		else
		{
			title = Utf8_PtrToString(desc);
			details = nullptr;

			if (!strcmp(ops->id, "pipeline"))
				type = ProjOperationType::Pipeline;
			else if (!strcmp(ops->id, "noop"))
				type = ProjOperationType::NoOperation;
			else
				type = ProjOperationType::Transformation;
		}

		opList->Add(gcnew ProjOperationDefinition(type, Utf8_PtrToString(ops->id), title, details, ops->proj));

		ops++;
	}

	return gcnew ProjOperationDefinition::ProjOperationDefinitionCollection(opList);
}


ProjOperationType ProjOperation::Type::get()
{
	if (m_type < ProjOperationType::Unknown)
	{
		ProjOperationDefinition^ def = nullptr;

		if (Name && ProjOperationDefinition::All->TryGetValue(Name, def))
		{
			m_type = def->Type;
		}
		else
			m_type = ProjOperationType::Unknown;
	}
	return m_type;
}