#include "Catty/Resource/ResourceServer.h"

#include "Catty/Core/Log.h"

namespace Catty
{

FResourceServer::~FResourceServer()
{
	Shutdown();
}

bool FResourceServer::OnInitialize()
{
	Enqueue([](FThreadedServer& /*Server*/)
	{
		CATTY_CORE_INFO("Resource server task executed on resource thread");
	});
	Flush();
	return true;
}

void FResourceServer::OnShutdown()
{
}

} // namespace Catty
