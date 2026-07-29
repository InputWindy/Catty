#pragma once

#include <Core/Export.h>
#include <Core/Object.h>
#include <Core/Resource/ResourceHandle.h>

#include <cstdint>
#include <string>

namespace Catty
{

class FPackage;

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
 * External-file asset (png / mesh / ...). Always Outer'd to an FPackage.
 * The FResource object is created synchronously with the package object table;
 * raw bytes are filled later by FResourceManager's internal loader (Pending → Ready/Failed).
 * Package JSON stores source path only — not binary content.
 *
 * Example:
 * ```
 *   Catty::FObjectRef Pkg = Catty::GetTransientPackage();
 *   Catty::FObjectRef Hero = Catty::NewObject<Catty::FResource>(...);
 *   Catty::RegisterResource(Hero);
 *   Catty::FlushResource(Hero);
 * ```
 */
CATTY_OBJECT()
class CATTY_API FResource : public FObject
{
	CATTY_GENERATED_BODY()

public:
	FResource(
		FPackage* InOuter,
		std::string InObjectName,
		FResourceId InId,
		EResourceType InType,
		std::string InSourcePath);
	virtual ~FResource() override;

	/** FGC pool TearDown — Unregister + ReleaseResourceId via ResourceManager, then ClearOuter. */
	static void StaticTearDown(FResource* Resource);

	// ---------------------------------------------------------------------------
	// Queries (C++ typed Id — FResourceId not a CATTY_FUNCTION return kind yet)
	// ---------------------------------------------------------------------------
	[[nodiscard]] FResourceId GetId() const { return Id; }

	// ---------------------------------------------------------------------------
	// Reflection — CATTY_FUNCTION / CATTY_PROPERTY (game / editor / Lua)
	// ---------------------------------------------------------------------------
	CATTY_FUNCTION()
	[[nodiscard]] std::uint64_t GetIdValue() const { return Id.Value; }
	CATTY_FUNCTION()
	[[nodiscard]] EResourceType GetType() const { return Type; }
	CATTY_FUNCTION()
	[[nodiscard]] const std::string& GetSourcePath() const { return SourcePath; }
	/** Alias for GetSourcePath (legacy name). */
	CATTY_FUNCTION()
	[[nodiscard]] const std::string& GetPath() const { return SourcePath; }

protected:
	CATTY_PROPERTY()
	EResourceType Type = EResourceType::Unknown;
	CATTY_PROPERTY()
	std::string SourcePath;

	// ---------------------------------------------------------------------------
	// Fields
	// ---------------------------------------------------------------------------
	FResourceId Id{};
};

} // namespace Catty
