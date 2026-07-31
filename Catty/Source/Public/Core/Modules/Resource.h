#pragma once

#include <Core/Export.h>
#include <Core/Object/Object.h>
#include <Core/Object/ObjectReflect.h>

#include <cstdint>
#include <string>

namespace Catty
{

class UPackage;
class FResourceManager;

CATTY_ENUM()
enum class EResourceLoadState : std::uint8_t
{
	Invalid = 0,
	Pending,
	Ready,
	Failed
};

/** High-level asset kind (extend as typed resources appear). */
CATTY_ENUM()
enum class EResourceType : std::uint8_t
{
	Unknown = 0,
	Raw,
	Texture,
	Mesh,
	Material,
	Shader,
	Audio,
	Data,
};

/**
 * External-file asset (png / mesh / ...).
 * Outer may be an UPackage (saved content) or null (runtime-only; PathName = ObjectName).
 * Created by the private Resource module; LoadState becomes Ready after BulkData import.
 */
CATTY_OBJECT()
class CATTY_API UResource : public UObject
{
	CATTY_GENERATED_BODY()

public:
	static constexpr int PoolSize = 64;

	UResource(
		UPackage* InOuter,
		std::string InObjectName,
		EResourceType InType,
		std::string InSourcePath);
	virtual ~UResource() override;

	void OnPoolTearDown() override;

	CATTY_FUNCTION()
	[[nodiscard]] EResourceType GetType() const { return Type; }
	CATTY_FUNCTION()
	[[nodiscard]] const std::string& GetSourcePath() const { return SourcePath; }
	/** Alias for GetSourcePath (legacy name). */
	CATTY_FUNCTION()
	[[nodiscard]] const std::string& GetPath() const { return SourcePath; }
	CATTY_FUNCTION()
	[[nodiscard]] EResourceLoadState GetLoadState() const { return LoadState; }

protected:
	friend class FResourceManager;

	void SetLoadState(EResourceLoadState InState) { LoadState = InState; }

	CATTY_PROPERTY()
	EResourceType Type = EResourceType::Unknown;
	CATTY_PROPERTY()
	std::string SourcePath;
	CATTY_PROPERTY()
	EResourceLoadState LoadState = EResourceLoadState::Pending;
};

/**
 * Typed texture asset shell (decode / GPU upload live in private ResourceIO Traits).
 * Not CATTY_OBJECT — Lua FLua_* wrappers only support UObject as sol base today.
 */
class CATTY_API UTextureResource : public UResource
{
public:
	static constexpr int PoolSize = 32;

	UTextureResource(
		UPackage* InOuter,
		std::string InObjectName,
		EResourceType InType,
		std::string InSourcePath);
	virtual ~UTextureResource() override;
};

} // namespace Catty
