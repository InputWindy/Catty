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
 * External-file asset export (png / mesh / ...). Always Outer'd to an FPackage.
 * The FResource object is created synchronously with the package/export table;
 * raw bytes are filled later by FResourceServer (Pending → Ready/Failed).
 * Package JSON stores source path only — not binary content.
 *
 * Example:
 * ```
 *   Catty::FPackage* Pkg = ResourceManager.GetTransientPackage();
 *   Catty::FResourceRef Hero = ResourceManager.CreateResource(
 *       *Pkg, "T_Hero", "Textures/T_Hero.png");
 *   ResourceManager.Flush(Hero); // wait for raw fill
 * ```
 */
class CATTY_API FResource : public FObject
{
public:
	FResource(
		FPackage* InOuter,
		std::string InObjectName,
		FResourceId InId,
		EResourceType InType,
		std::string InSourcePath);
	~FResource() override;

	[[nodiscard]] FResourceId GetId() const { return Id; }
	[[nodiscard]] EResourceType GetType() const { return Type; }

	/** Disk path of the external asset file (not the Package.ObjectName path). */
	[[nodiscard]] const std::string& GetSourcePath() const { return SourcePath; }

	/** Alias for GetSourcePath (legacy name). */
	[[nodiscard]] const std::string& GetPath() const { return SourcePath; }

protected:
	FResourceId Id{};
	EResourceType Type = EResourceType::Unknown;
	std::string SourcePath;
};

/**
 * Refcounted handle to FResource (same semantics as FObjectRef).
 */
class CATTY_API FResourceRef
{
public:
	FResourceRef() = default;

	explicit FResourceRef(FResource* InResource);
	FResourceRef(const FResourceRef& Other);
	FResourceRef(FResourceRef&& Other) noexcept;
	~FResourceRef();

	FResourceRef& operator=(const FResourceRef& Other);
	FResourceRef& operator=(FResourceRef&& Other) noexcept;

	[[nodiscard]] bool IsValid() const { return Resource != nullptr; }
	[[nodiscard]] FResource* Get() const { return Resource; }
	[[nodiscard]] FResource& operator*() const { return *Resource; }
	[[nodiscard]] FResource* operator->() const { return Resource; }

	void Reset();

private:
	FResource* Resource = nullptr;
};

} // namespace Catty
