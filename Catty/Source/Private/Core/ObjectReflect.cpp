#include <Core/ObjectReflect.h>
#include <Core/Object.h>
#include <algorithm>
namespace Catty
{

FPropertyValue FPropertyValue::FromBool(bool V)
{
	FPropertyValue Out;
	Out.Type = EPropertyType::Bool;
	Out.BoolValue = V;
	return Out;
}

FPropertyValue FPropertyValue::FromInt(std::int64_t V)
{
	FPropertyValue Out;
	Out.Type = EPropertyType::Int64;
	Out.IntValue = V;
	return Out;
}

FPropertyValue FPropertyValue::FromUInt(std::uint64_t V)
{
	FPropertyValue Out;
	Out.Type = EPropertyType::UInt64;
	Out.UIntValue = V;
	return Out;
}

FPropertyValue FPropertyValue::FromFloat(double V)
{
	FPropertyValue Out;
	Out.Type = EPropertyType::Double;
	Out.FloatValue = V;
	return Out;
}

FPropertyValue FPropertyValue::FromString(std::string V)
{
	FPropertyValue Out;
	Out.Type = EPropertyType::String;
	Out.StringValue = std::move(V);
	return Out;
}

void FPropertyValue::ReleaseObjectValue()
{
	if (Type == EPropertyType::ObjectRef && ObjectValue)
	{
		ObjectValue->ReleaseRef();
		ObjectValue = nullptr;
	}
}

void FPropertyValue::CopyFrom(const FPropertyValue& Other)
{
	Type = Other.Type;
	BoolValue = Other.BoolValue;
	IntValue = Other.IntValue;
	UIntValue = Other.UIntValue;
	FloatValue = Other.FloatValue;
	StringValue = Other.StringValue;
	ObjectValue = nullptr;
	if (Type == EPropertyType::ObjectRef && Other.ObjectValue)
	{
		ObjectValue = Other.ObjectValue;
		ObjectValue->AddRef();
	}
}

void FPropertyValue::MoveFrom(FPropertyValue&& Other) noexcept
{
	Type = Other.Type;
	BoolValue = Other.BoolValue;
	IntValue = Other.IntValue;
	UIntValue = Other.UIntValue;
	FloatValue = Other.FloatValue;
	StringValue = std::move(Other.StringValue);
	ObjectValue = Other.ObjectValue;
	Other.ObjectValue = nullptr;
	if (Other.Type == EPropertyType::ObjectRef)
	{
		Other.Type = EPropertyType::Bool;
	}
}

FPropertyValue::FPropertyValue(const FPropertyValue& Other)
{
	CopyFrom(Other);
}

FPropertyValue::FPropertyValue(FPropertyValue&& Other) noexcept
{
	MoveFrom(std::move(Other));
}

FPropertyValue::~FPropertyValue()
{
	ReleaseObjectValue();
}

FPropertyValue& FPropertyValue::operator=(const FPropertyValue& Other)
{
	if (this != &Other)
	{
		ReleaseObjectValue();
		CopyFrom(Other);
	}
	return *this;
}

FPropertyValue& FPropertyValue::operator=(FPropertyValue&& Other) noexcept
{
	if (this != &Other)
	{
		ReleaseObjectValue();
		MoveFrom(std::move(Other));
	}
	return *this;
}

FPropertyValue FPropertyValue::FromObject(UObject* InObject)
{
	FPropertyValue Out;
	Out.Type = EPropertyType::ObjectRef;
	Out.ObjectValue = InObject;
	if (Out.ObjectValue)
	{
		Out.ObjectValue->AddRef();
	}
	return Out;
}

const FProperty* FObjectType::FindProperty(std::string_view InName) const
{
	for (std::size_t I = 0; I < PropertyCount; ++I)
	{
		if (Properties[I].Name && InName == Properties[I].Name)
		{
			return &Properties[I];
		}
	}
	return nullptr;
}

const FFunction* FObjectType::FindFunction(std::string_view InName) const
{
	for (std::size_t I = 0; I < FunctionCount; ++I)
	{
		if (Functions[I].Name && InName == Functions[I].Name)
		{
			return &Functions[I];
		}
	}
	return nullptr;
}

const FProperty* FObjectType::FindPropertyInHierarchy(std::string_view InName) const
{
	for (const FObjectType* Type = this; Type; Type = Type->Super)
	{
		if (const FProperty* Prop = Type->FindProperty(InName))
		{
			return Prop;
		}
	}
	return nullptr;
}

const FFunction* FObjectType::FindFunctionInHierarchy(std::string_view InName) const
{
	for (const FObjectType* Type = this; Type; Type = Type->Super)
	{
		if (const FFunction* Func = Type->FindFunction(InName))
		{
			return Func;
		}
	}
	return nullptr;
}

void FObjectType::GatherPropertiesInHierarchy(std::vector<const FProperty*>& Out) const
{
	std::vector<const FObjectType*> Chain;
	for (const FObjectType* Type = this; Type; Type = Type->Super)
	{
		Chain.push_back(Type);
	}
	std::reverse(Chain.begin(), Chain.end());
	for (const FObjectType* Type : Chain)
	{
		for (std::size_t I = 0; I < Type->PropertyCount; ++I)
		{
			Out.push_back(&Type->Properties[I]);
		}
	}
}

void FObjectType::GatherFunctionsInHierarchy(std::vector<const FFunction*>& Out) const
{
	std::vector<const FObjectType*> Chain;
	for (const FObjectType* Type = this; Type; Type = Type->Super)
	{
		Chain.push_back(Type);
	}
	std::reverse(Chain.begin(), Chain.end());
	for (const FObjectType* Type : Chain)
	{
		for (std::size_t I = 0; I < Type->FunctionCount; ++I)
		{
			Out.push_back(&Type->Functions[I]);
		}
	}
}

FObjectTypeRegistry& FObjectTypeRegistry::Get()
{
	static FObjectTypeRegistry Instance;
	return Instance;
}

void FObjectTypeRegistry::RegisterType(const FObjectType& Type)
{
	if (!Type.Name)
	{
		return;
	}
	const std::string Key(Type.Name);
	if (NameToType.find(Key) != NameToType.end())
	{
		return;
	}
	Types.push_back(&Type);
	NameToType.emplace(Key, &Type);
}

const FObjectType* FObjectTypeRegistry::FindType(std::string_view Name) const
{
	const auto It = NameToType.find(std::string(Name));
	return It == NameToType.end() ? nullptr : It->second;
}

const FStructProperty* FStructType::FindProperty(std::string_view InName) const
{
	for (std::size_t I = 0; I < PropertyCount; ++I)
	{
		if (Properties[I].Name && InName == Properties[I].Name)
		{
			return &Properties[I];
		}
	}
	return nullptr;
}

FStructTypeRegistry& FStructTypeRegistry::Get()
{
	static FStructTypeRegistry Instance;
	return Instance;
}

void FStructTypeRegistry::RegisterType(const FStructType& Type)
{
	if (!Type.Name)
	{
		return;
	}
	const std::string Key(Type.Name);
	if (NameToType.find(Key) != NameToType.end())
	{
		return;
	}
	Types.push_back(&Type);
	NameToType.emplace(Key, &Type);
}

const FStructType* FStructTypeRegistry::FindType(std::string_view Name) const
{
	const auto It = NameToType.find(std::string(Name));
	return It == NameToType.end() ? nullptr : It->second;
}

bool GetStructPropertyValue(
	const FStructType& Type,
	const void* Struct,
	std::string_view PropertyName,
	FPropertyValue& OutValue)
{
	if (!Struct)
	{
		return false;
	}
	const FStructProperty* Prop = Type.FindProperty(PropertyName);
	if (!Prop || !Prop->Getter)
	{
		return false;
	}
	return Prop->Getter(Struct, OutValue);
}

bool SetStructPropertyValue(
	const FStructType& Type,
	void* Struct,
	std::string_view PropertyName,
	const FPropertyValue& Value)
{
	if (!Struct)
	{
		return false;
	}
	const FStructProperty* Prop = Type.FindProperty(PropertyName);
	if (!Prop || !Prop->Setter)
	{
		return false;
	}
	return Prop->Setter(Struct, Value);
}

const FEnumValue* FEnumType::FindByName(std::string_view InName) const
{
	for (std::size_t I = 0; I < ValueCount; ++I)
	{
		if (Values[I].Name && InName == Values[I].Name)
		{
			return &Values[I];
		}
	}
	return nullptr;
}

const FEnumValue* FEnumType::FindByValue(std::int64_t InValue) const
{
	for (std::size_t I = 0; I < ValueCount; ++I)
	{
		if (Values[I].Value == InValue)
		{
			return &Values[I];
		}
	}
	return nullptr;
}

FEnumTypeRegistry& FEnumTypeRegistry::Get()
{
	static FEnumTypeRegistry Instance;
	return Instance;
}

void FEnumTypeRegistry::RegisterType(const FEnumType& Type)
{
	if (!Type.Name)
	{
		return;
	}
	const std::string Key(Type.Name);
	if (NameToType.find(Key) != NameToType.end())
	{
		return;
	}
	Types.push_back(&Type);
	NameToType.emplace(Key, &Type);
}

const FEnumType* FEnumTypeRegistry::FindType(std::string_view Name) const
{
	const auto It = NameToType.find(std::string(Name));
	return It == NameToType.end() ? nullptr : It->second;
}
} // namespace Catty
