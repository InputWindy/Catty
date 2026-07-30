#pragma once

/**
 * UObject / struct / enum runtime reflection (editor properties / blueprint call).
 *
 * Object:
 *   CATTY_OBJECT()
 *   class FMyAsset : public UObject
 *   {
 *   	CATTY_GENERATED_BODY()
 *   public:
 *   	// Optional FGC pool: PoolSize + StaticTearDown (codegen registers).
 *   	static constexpr int PoolSize = 16;
 *   	static void StaticTearDown(FMyAsset* Self);
 *   	CATTY_PROPERTY()
 *   	std::string DisplayName;
 *   	CATTY_FUNCTION()
 *   	void Reset(std::string Reason);
 *   };
 *
 * Struct:
 *   CATTY_STRUCT()
 *   struct FPoint
 *   {
 *   	CATTY_GENERATED_STRUCT_BODY()
 *   	CATTY_PROPERTY()
 *   	float X = 0.0f;
 *   };
 *
 * Enum:
 *   CATTY_ENUM()
 *   enum class EKind : std::uint8_t
 *   {
 *   	None = 0,
 *   	A,
 *   	B,
 *   };
 *
 * Codegen: Tools/object_reflect_codegen.py → Source/Generated/ObjectReflect*.gen.*
 * Lua usertypes are generated from the same scan (ObjectRef ↔ catty.object / package / …).
 */

#include <Core/Export.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace Catty
{

class UObject;

/** Supported property / CallFunction value types. */
enum class EPropertyType : std::uint8_t
{
	Bool = 0,
	Int32,
	UInt32,
	Int64,
	UInt64,
	Float,
	Double,
	String,
	EnumInt32,
	/** FObjectRef (refcounted). Stored as UObject* with AddRef/ReleaseRef. */
	ObjectRef,
};

/** Editor-facing property flags (phase 1: Edit only). */
enum class EPropertyFlags : std::uint32_t
{
	None = 0,
	Edit = 1u << 0,
};

[[nodiscard]] constexpr EPropertyFlags operator|(EPropertyFlags A, EPropertyFlags B)
{
	return static_cast<EPropertyFlags>(static_cast<std::uint32_t>(A) | static_cast<std::uint32_t>(B));
}

[[nodiscard]] constexpr bool HasAnyPropertyFlags(EPropertyFlags Value, EPropertyFlags Test)
{
	return (static_cast<std::uint32_t>(Value) & static_cast<std::uint32_t>(Test)) != 0;
}

/**
 * Type-erased value for Get/SetPropertyValue and CallFunction.
 * ObjectRef type owns one AddRef while ObjectValue is non-null.
 */
struct CATTY_API FPropertyValue
{
	EPropertyType Type = EPropertyType::Bool;

	bool BoolValue = false;
	std::int64_t IntValue = 0;
	std::uint64_t UIntValue = 0;
	double FloatValue = 0.0;
	std::string StringValue;
	UObject* ObjectValue = nullptr;

	FPropertyValue() = default;
	FPropertyValue(const FPropertyValue& Other);
	FPropertyValue(FPropertyValue&& Other) noexcept;
	~FPropertyValue();

	FPropertyValue& operator=(const FPropertyValue& Other);
	FPropertyValue& operator=(FPropertyValue&& Other) noexcept;

	[[nodiscard]] static FPropertyValue FromBool(bool V);
	[[nodiscard]] static FPropertyValue FromInt(std::int64_t V);
	[[nodiscard]] static FPropertyValue FromUInt(std::uint64_t V);
	[[nodiscard]] static FPropertyValue FromFloat(double V);
	[[nodiscard]] static FPropertyValue FromString(std::string V);
	/** AddRefs InObject when non-null. */
	[[nodiscard]] static FPropertyValue FromObject(UObject* InObject);

	[[nodiscard]] UObject* GetObjectPtr() const
	{
		return Type == EPropertyType::ObjectRef ? ObjectValue : nullptr;
	}

private:
	void ReleaseObjectValue();
	void CopyFrom(const FPropertyValue& Other);
	void MoveFrom(FPropertyValue&& Other) noexcept;
};

/** Convert common C++ values into FPropertyValue for CallFunction packs. */
[[nodiscard]] inline FPropertyValue ToPropertyValue(bool V)
{
	return FPropertyValue::FromBool(V);
}
[[nodiscard]] inline FPropertyValue ToPropertyValue(std::int32_t V)
{
	FPropertyValue Out = FPropertyValue::FromInt(V);
	Out.Type = EPropertyType::Int32;
	return Out;
}
[[nodiscard]] inline FPropertyValue ToPropertyValue(std::uint32_t V)
{
	FPropertyValue Out = FPropertyValue::FromInt(static_cast<std::int64_t>(V));
	Out.Type = EPropertyType::UInt32;
	return Out;
}
[[nodiscard]] inline FPropertyValue ToPropertyValue(std::int64_t V)
{
	return FPropertyValue::FromInt(V);
}
[[nodiscard]] inline FPropertyValue ToPropertyValue(std::uint64_t V)
{
	return FPropertyValue::FromUInt(V);
}
[[nodiscard]] inline FPropertyValue ToPropertyValue(float V)
{
	FPropertyValue Out = FPropertyValue::FromFloat(V);
	Out.Type = EPropertyType::Float;
	return Out;
}
[[nodiscard]] inline FPropertyValue ToPropertyValue(double V)
{
	return FPropertyValue::FromFloat(V);
}
[[nodiscard]] inline FPropertyValue ToPropertyValue(const std::string& V)
{
	return FPropertyValue::FromString(V);
}
[[nodiscard]] inline FPropertyValue ToPropertyValue(std::string_view V)
{
	return FPropertyValue::FromString(std::string(V));
}
[[nodiscard]] inline FPropertyValue ToPropertyValue(const char* V)
{
	return FPropertyValue::FromString(V ? std::string(V) : std::string());
}
[[nodiscard]] inline FPropertyValue ToPropertyValue(FPropertyValue V)
{
	return V;
}
template <typename TEnum>
[[nodiscard]] inline std::enable_if_t<std::is_enum_v<TEnum>, FPropertyValue> ToPropertyValue(TEnum V)
{
	FPropertyValue Out = FPropertyValue::FromInt(static_cast<std::int64_t>(V));
	Out.Type = EPropertyType::EnumInt32;
	return Out;
}

using FPropertyGetterFn = bool (*)(const UObject* Object, FPropertyValue& OutValue);
using FPropertySetterFn = bool (*)(UObject* Object, const FPropertyValue& Value);
/** Args may be null when ArgCount == 0. OutReturn may be null (ignored for void). */
using FFunctionInvokeFn = bool (*)(
	UObject* Object,
	const FPropertyValue* Args,
	std::size_t ArgCount,
	FPropertyValue* OutReturn);

struct FProperty
{
	const char* Name = nullptr;
	EPropertyType Type = EPropertyType::Bool;
	EPropertyFlags Flags = EPropertyFlags::Edit;
	FPropertyGetterFn Getter = nullptr;
	FPropertySetterFn Setter = nullptr;
};

struct FFunction
{
	const char* Name = nullptr;
	/** Expected parameter types (length = ParamCount); null when ParamCount == 0. */
	const EPropertyType* ParamTypes = nullptr;
	std::size_t ParamCount = 0;
	/** Meaningful when bHasReturn is true. */
	EPropertyType ReturnType = EPropertyType::Bool;
	bool bHasReturn = false;
	FFunctionInvokeFn Invoke = nullptr;
};

struct FObjectType
{
	const char* Name = nullptr;
	const FObjectType* Super = nullptr;
	const FProperty* Properties = nullptr;
	std::size_t PropertyCount = 0;
	const FFunction* Functions = nullptr;
	std::size_t FunctionCount = 0;

	[[nodiscard]] const FProperty* FindProperty(std::string_view InName) const;
	[[nodiscard]] const FFunction* FindFunction(std::string_view InName) const;

	/** Walk Super chain; first match wins (derived overrides). */
	[[nodiscard]] const FProperty* FindPropertyInHierarchy(std::string_view InName) const;
	[[nodiscard]] const FFunction* FindFunctionInHierarchy(std::string_view InName) const;

	void GatherPropertiesInHierarchy(std::vector<const FProperty*>& Out) const;
	void GatherFunctionsInHierarchy(std::vector<const FFunction*>& Out) const;
};

/** Friend of every CATTY_OBJECT type; method bodies live in ObjectReflectTypes.gen.cpp. */
struct FObjectReflectDetail;

class CATTY_API FObjectTypeRegistry
{
public:
	static FObjectTypeRegistry& Get();

	void RegisterType(const FObjectType& Type);
	[[nodiscard]] const FObjectType* FindType(std::string_view Name) const;
	[[nodiscard]] const std::vector<const FObjectType*>& GetTypes() const { return Types; }

	/** Implemented in ObjectReflectTypes.gen.cpp. */
	static void RegisterGeneratedTypes();

private:
	std::vector<const FObjectType*> Types;
	std::unordered_map<std::string, const FObjectType*> NameToType;
};

// ---------------------------------------------------------------------------
// Struct reflection
// ---------------------------------------------------------------------------

using FStructPropertyGetterFn = bool (*)(const void* Struct, FPropertyValue& OutValue);
using FStructPropertySetterFn = bool (*)(void* Struct, const FPropertyValue& Value);

struct FStructProperty
{
	const char* Name = nullptr;
	EPropertyType Type = EPropertyType::Bool;
	EPropertyFlags Flags = EPropertyFlags::Edit;
	FStructPropertyGetterFn Getter = nullptr;
	FStructPropertySetterFn Setter = nullptr;
};

struct FStructType
{
	const char* Name = nullptr;
	const FStructProperty* Properties = nullptr;
	std::size_t PropertyCount = 0;
	std::size_t Size = 0;

	[[nodiscard]] const FStructProperty* FindProperty(std::string_view InName) const;
};

/** Friend of every CATTY_STRUCT type; method bodies live in ObjectReflectTypes.gen.cpp. */
struct FStructReflectDetail;

class CATTY_API FStructTypeRegistry
{
public:
	static FStructTypeRegistry& Get();

	void RegisterType(const FStructType& Type);
	[[nodiscard]] const FStructType* FindType(std::string_view Name) const;
	[[nodiscard]] const std::vector<const FStructType*>& GetTypes() const { return Types; }

	static void RegisterGeneratedTypes();

private:
	std::vector<const FStructType*> Types;
	std::unordered_map<std::string, const FStructType*> NameToType;
};

[[nodiscard]] CATTY_API bool GetStructPropertyValue(
	const FStructType& Type,
	const void* Struct,
	std::string_view PropertyName,
	FPropertyValue& OutValue);

[[nodiscard]] CATTY_API bool SetStructPropertyValue(
	const FStructType& Type,
	void* Struct,
	std::string_view PropertyName,
	const FPropertyValue& Value);

// ---------------------------------------------------------------------------
// Enum reflection
// ---------------------------------------------------------------------------

struct FEnumValue
{
	const char* Name = nullptr;
	std::int64_t Value = 0;
};

struct FEnumType
{
	const char* Name = nullptr;
	const FEnumValue* Values = nullptr;
	std::size_t ValueCount = 0;

	[[nodiscard]] const FEnumValue* FindByName(std::string_view InName) const;
	[[nodiscard]] const FEnumValue* FindByValue(std::int64_t InValue) const;
};

class CATTY_API FEnumTypeRegistry
{
public:
	static FEnumTypeRegistry& Get();

	void RegisterType(const FEnumType& Type);
	[[nodiscard]] const FEnumType* FindType(std::string_view Name) const;
	[[nodiscard]] const std::vector<const FEnumType*>& GetTypes() const { return Types; }

	static void RegisterGeneratedTypes();

private:
	std::vector<const FEnumType*> Types;
	std::unordered_map<std::string, const FEnumType*> NameToType;
};

// ---------------------------------------------------------------------------
// Macros
// ---------------------------------------------------------------------------

/** Line above class / struct — scanner marker (expands to nothing). */
#define CATTY_OBJECT(...)

/**
 * Inside class body (typically near the top). Declares StaticType / GetObjectType
 * and grants codegen access to protected members.
 */
#define CATTY_GENERATED_BODY() \
	friend struct ::Catty::FObjectReflectDetail; \
public: \
	static const ::Catty::FObjectType& StaticType(); \
	virtual const ::Catty::FObjectType& GetObjectType() const;

/** Line above plain struct — scanner marker (expands to nothing). */
#define CATTY_STRUCT(...)

/**
 * Inside struct body. Declares StaticType and grants codegen access to members.
 */
#define CATTY_GENERATED_STRUCT_BODY() \
	friend struct ::Catty::FStructReflectDetail; \
	static const ::Catty::FStructType& StaticType();

/** Line above enum / enum class — scanner marker (expands to nothing). */
#define CATTY_ENUM(...)

/** Mark the next data member as a reflected property (scanner marker). */
#define CATTY_PROPERTY(...)

/** Mark the next member function as a reflected callable (scanner marker). */
#define CATTY_FUNCTION(...)

} // namespace Catty
