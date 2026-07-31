#include <Core/Object/ObjectReflect.h>

#include <Core/Object/Object.h>

#include <Core/Object/Package.h>
#include <Core/Extension/Resource.h>

#include <ObjectReflectTypes.gen.h>



#include <cassert>

#include <cstring>



namespace Catty

{



namespace

{



void SelfTestObjectReflect()

{

	EnsureObjectReflectRegistered();



	const FObjectType& ObjectType = UObject::StaticType();

	const FObjectType& PackageType = UPackage::StaticType();

	const FObjectType& ResourceType = UResource::StaticType();



	assert(std::strcmp(ObjectType.Name, "Catty::UObject") == 0);

	assert(PackageType.Super == &ObjectType);

	assert(ResourceType.Super == &ObjectType);

	assert(ObjectType.FindFunction("IsPendingKill") != nullptr);

	assert(ObjectType.FindProperty("ObjectName") != nullptr);

	assert(ResourceType.FindProperty("SourcePath") != nullptr);

	assert(FObjectTypeRegistry::Get().FindType("Catty::UResource") != nullptr);



	UPackage TransientPkg("/Temp/ObjectReflectSelfTest", EPackageFlags::Transient);



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

	// Enum reflection

	const FEnumType* ResourceEnum = FEnumTypeRegistry::Get().FindType("Catty::EResourceType");

	assert(ResourceEnum != nullptr);

	assert(ResourceEnum->FindByName("Texture") != nullptr);
	assert(ResourceEnum->FindByName("Texture")->Value == static_cast<std::int64_t>(EResourceType::Texture));
	assert(ResourceEnum->FindByName("Texture2D") != nullptr);
	assert(ResourceEnum->FindByName("Texture2D")->Value == static_cast<std::int64_t>(EResourceType::Texture2D));
	assert(ResourceEnum->FindByName("TextureCube") != nullptr);
	assert(ResourceEnum->FindByName("TextureCube")->Value == static_cast<std::int64_t>(EResourceType::TextureCube));
	assert(ResourceEnum->FindByValue(static_cast<std::int64_t>(EResourceType::Mesh)) != nullptr);

	const FEnumType* TexDimEnum = FEnumTypeRegistry::Get().FindType("Catty::ETextureDimension");
	assert(TexDimEnum != nullptr);
	assert(TexDimEnum->FindByName("Cube") != nullptr);



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

