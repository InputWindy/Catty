#include "Core/ObjectReflect.h"

#include "Resource/Object.h"

#include "Resource/Package.h"

#include "Resource/Resource.h"

#include "Resource/ResourceHandle.h"

#include "ObjectReflectTypes.gen.h"



#include <cassert>

#include <cstring>



namespace Catty

{



namespace

{



void SelfTestObjectReflect()

{

	EnsureObjectReflectRegistered();



	const FObjectType& ObjectType = FObject::StaticType();

	const FObjectType& PackageType = FPackage::StaticType();

	const FObjectType& ResourceType = FResource::StaticType();



	assert(std::strcmp(ObjectType.Name, "Catty::FObject") == 0);

	assert(PackageType.Super == &ObjectType);

	assert(ResourceType.Super == &ObjectType);

	assert(ObjectType.FindFunction("MarkPendingKill") != nullptr);

	assert(ObjectType.FindProperty("ObjectName") != nullptr);

	assert(ResourceType.FindProperty("SourcePath") != nullptr);

	assert(FObjectTypeRegistry::Get().FindType("Catty::FResource") != nullptr);



	FPackage TransientPkg("/Temp/ObjectReflectSelfTest", EPackageFlags::Transient);



	FPropertyValue NameValue;

	assert(TransientPkg.GetPropertyValue("ObjectName", NameValue));

	assert(NameValue.Type == EPropertyType::String);

	assert(NameValue.StringValue == "/Temp/ObjectReflectSelfTest");



	assert(TransientPkg.SetPropertyValue("ObjectName", FPropertyValue::FromString("Renamed")));

	assert(TransientPkg.GetName() == "Renamed");



	FPropertyValue PathValue;
	assert(TransientPkg.CallFunction("GetFilePath", nullptr, 0, &PathValue));
	assert(PathValue.Type == EPropertyType::String);

	FPropertyValue OuterRet;
	assert(TransientPkg.CallFunction("GetOuter", nullptr, 0, &OuterRet));
	assert(OuterRet.Type == EPropertyType::ObjectRef);
	assert(OuterRet.GetObjectPtr() == nullptr);

	FPropertyValue Pending;
	assert(TransientPkg.CallFunction("IsPendingKill", nullptr, 0, &Pending));
	assert(Pending.Type == EPropertyType::Bool);
	assert(Pending.BoolValue == false);

	FPropertyValue NameRet;
	assert(TransientPkg.CallFunction("GetName", nullptr, 0, &NameRet));
	assert(NameRet.Type == EPropertyType::String);
	assert(NameRet.StringValue == "Renamed");



	// Struct reflection

	const FStructType& ResourceIdType = FResourceId::StaticType();

	assert(std::strcmp(ResourceIdType.Name, "Catty::FResourceId") == 0);

	assert(ResourceIdType.FindProperty("Value") != nullptr);

	assert(FStructTypeRegistry::Get().FindType("Catty::FResourceId") != nullptr);



	FResourceId Id{};

	Id.Value = 42;

	FPropertyValue IdValue;

	assert(GetStructPropertyValue(ResourceIdType, &Id, "Value", IdValue));

	assert(IdValue.Type == EPropertyType::UInt64);

	assert(IdValue.UIntValue == 42);

	assert(SetStructPropertyValue(ResourceIdType, &Id, "Value", FPropertyValue::FromUInt(99)));

	assert(Id.Value == 99);



	// Enum reflection

	const FEnumType* ResourceEnum = FEnumTypeRegistry::Get().FindType("Catty::EResourceType");

	assert(ResourceEnum != nullptr);

	assert(ResourceEnum->FindByName("Texture") != nullptr);

	assert(ResourceEnum->FindByName("Texture")->Value == static_cast<std::int64_t>(EResourceType::Texture));

	assert(ResourceEnum->FindByValue(static_cast<std::int64_t>(EResourceType::Mesh)) != nullptr);



	const FEnumType* ObjectFlagsEnum = FEnumTypeRegistry::Get().FindType("Catty::EObjectFlags");

	assert(ObjectFlagsEnum != nullptr);

	assert(ObjectFlagsEnum->FindByName("PendingKill")->Value == static_cast<std::int64_t>(EObjectFlags::PendingKill));



	const FEnumType* PackageFlagsEnum = FEnumTypeRegistry::Get().FindType("Catty::EPackageFlags");

	assert(PackageFlagsEnum != nullptr);

	assert(PackageFlagsEnum->FindByName("Transient")->Value == static_cast<std::int64_t>(EPackageFlags::Transient));



	const FEnumType* LoadStateEnum = FEnumTypeRegistry::Get().FindType("Catty::EResourceLoadState");

	assert(LoadStateEnum != nullptr);

	assert(LoadStateEnum->FindByName("Ready")->Value == static_cast<std::int64_t>(EResourceLoadState::Ready));

}



#if defined(_DEBUG) || !defined(NDEBUG)

struct FObjectReflectSelfTestRunner

{

	FObjectReflectSelfTestRunner()

	{

		SelfTestObjectReflect();

	}

};



static FObjectReflectSelfTestRunner GObjectReflectSelfTestRunner;

#endif



} // namespace



} // namespace Catty

