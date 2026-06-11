#pragma once

#include <Arduino.h>
#include <string>
#include <vector>

void bridge_olp_pushHaState(const char *entityId, const char *state);
void bridge_olp_pushHaStateAttrs(const char *entityId, const char *attributesJson);

namespace bridge_ha {



void init();

void tick();



bool configured();

bool callService(const std::string &domain, const std::string &service, const std::string &entityId,

                 const std::string &serviceDataJson = {});

void setSubscribedEntities(const std::vector<std::string> &entityIds);

/** Fetch one entity from HA and push state/attrs to the linked remote. */
void pollEntityNow(const std::string &entityId);

/** Poll every subscribed entity immediately (e.g. after subscribe list changes). */
void pollSubscribedNow();

bool fetchEntityState(const std::string &entityId, std::string &stateOut, std::string *attrsOut = nullptr);

/** GET /api/ — returns HTTP status or negative on TCP failure. */
int testApiConnection(std::string &detailOut);

struct HaDiagResult {
  bool tcpOk = false;
  int httpCode = -99;
  String bridgeIp;
  String gatewayIp;
  String haIp;
  std::string detail;
};

HaDiagResult diagnose();

} // namespace bridge_ha

