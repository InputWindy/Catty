#pragma once

#include "Catty/Core/Export.h"
#include "Catty/Resource/Object.h"
#include "Catty/Resource/ResourceHandle.h"

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
 *   Catty::FObjectRef Pkg = ResourceManager.GetTransientPackage();
 *   Catty::FObjectRef Hero = ResourceManager.CreateResource(
 *       Pkg, "T_Hero", "Textures/T_Hero.png");
 *   if (Catty::FResource* Res = Hero.Cast<Catty::FResource>())
 *   {
 *       ResourceManager.Flush(Hero);
 *   }
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

	[[nodiscard]] FResourceId GetId() const { return Id; }
	[[nodiscard]] EResourceType GetType() const { return Type; }

	/** Disk path of the external asset file (not the Package.ObjectName path). */
	[[nodiscard]] const std::string& GetSourcePath() const { return SourcePath; }

	/** Alias for GetSourcePath (legacy name). */
	[[nodiscard]] const std::string& GetPath() const { return SourcePath; }

protected:
	FResourceId Id{};
	CATTY_PROPERTY()
	EResourceType Type = EResourceType::Unknown;
	CATTY_PROPERTY()
	std::string SourcePath;
};

} // namespace Catty
