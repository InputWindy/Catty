#include "Catty/Core/Reflect.h"

namespace Catty
{
namespace ReflectPrivate
{

struct FReflectSmoke
{
	int X = 0;
	float Y = 0.f;

	int Scale(int Factor) const
	{
		return X * Factor;
	}
};

} // namespace ReflectPrivate
} // namespace Catty

CATTY_REFLECT_BEGIN(Catty::ReflectPrivate::FReflectSmoke)
	CATTY_REFLECT_PROPERTY(X)
	CATTY_REFLECT_PROPERTY(Y)
	CATTY_REFLECT_FUNCTION(Scale)
CATTY_REFLECT_END

namespace Catty
{
namespace ReflectPrivate
{

static_assert(TIsReflectable_v<FReflectSmoke>);
static_assert(ReflectMemberCount<FReflectSmoke>() == 3);
static_assert(ReflectTypeName<FReflectSmoke>().find("FReflectSmoke") != std::string_view::npos);

} // namespace ReflectPrivate
} // namespace Catty
