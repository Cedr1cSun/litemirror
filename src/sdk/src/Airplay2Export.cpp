#include "Airplay2Head.h"
#include "FgAirplayServer.h"

void* fgServerStart(const char serverName[AIRPLAY_NAME_LEN], 
	unsigned int raopPort, unsigned int airplayPort,
	IAirServerCallback* callback) 
{
	FgAirplayServer* pServer = new FgAirplayServer();
	if (pServer->start(serverName, raopPort, airplayPort, callback) != 0) {
		delete pServer;
		return NULL;
	}
	return pServer;
}

void fgServerStop(void* handle) 
{
	if (handle != NULL) {
		FgAirplayServer* pServer = (FgAirplayServer*)handle;
		pServer->stop();

		delete pServer;
		pServer = NULL;
	}
}

float fgServerScale(void* handle, float fRatio)
{
	if (handle != NULL) {
		FgAirplayServer* pServer = (FgAirplayServer*)handle;
		return pServer->setScale(fRatio);
	}

	return 1.0f;
}
