#pragma once

#include "../../macros.hpp"
#include "../../helpers/memory/Memory.hpp"
#include "../../helpers/AsyncDialogBox.hpp"
#include <vector>
#include <wayland-server-core.h>
#include <sys/types.h>
#include "../../helpers/defer/Promise.hpp"

// NOLINTNEXTLINE
namespace re2 {
    class RE2;
};

enum eDynamicPermissionType : uint8_t {
    PERMISSION_TYPE_UNKNOWN = 0,
    PERMISSION_TYPE_SCREENCOPY,
    PERMISSION_TYPE_PLUGIN,
    PERMISSION_TYPE_KEYBOARD,
};

enum eDynamicPermissionRuleSource : uint8_t {
    PERMISSION_RULE_SOURCE_UNKNOWN = 0,
    PERMISSION_RULE_SOURCE_CONFIG,
    PERMISSION_RULE_SOURCE_RUNTIME_USER,
};

enum eDynamicPermissionAllowMode : uint8_t {
    PERMISSION_RULE_ALLOW_MODE_UNKNOWN = 0,
    PERMISSION_RULE_ALLOW_MODE_DENY,
    PERMISSION_RULE_ALLOW_MODE_ASK,
    PERMISSION_RULE_ALLOW_MODE_ALLOW,
    PERMISSION_RULE_ALLOW_MODE_PENDING, // popup is open
};

// NOLINTNEXTLINE
enum eSpecialPidTypes : int {
    SPECIAL_PID_TYPE_CONFIG = -3,
    SPECIAL_PID_TYPE_NONE   = -2,
};

class CDynamicPermissionRule;

struct SDynamicPermissionRuleDestroyWrapper {
    wl_listener             listener;
    CDynamicPermissionRule* parent = nullptr;
};

class CDynamicPermissionRule {
  public:
    ~CDynamicPermissionRule();

    wl_client* client() const;

  private:
    // config rule
    CDynamicPermissionRule(const std::string& binaryPathRegex, eDynamicPermissionType type, eDynamicPermissionAllowMode defaultAllowMode = PERMISSION_RULE_ALLOW_MODE_ASK);
    // user rule
    CDynamicPermissionRule(wl_client* const client, eDynamicPermissionType type, eDynamicPermissionAllowMode defaultAllowMode = PERMISSION_RULE_ALLOW_MODE_ASK);

    const eDynamicPermissionType                      m_type       = PERMISSION_TYPE_UNKNOWN;
    const eDynamicPermissionRuleSource                m_source     = PERMISSION_RULE_SOURCE_UNKNOWN;
    wl_client* const                                  m_client     = nullptr;
    std::string                                       m_binaryPath = "";
    UP<re2::RE2>                                      m_binaryRegex;
    std::string                                       m_keyString = "";
    pid_t                                             m_pid       = 0;

    eDynamicPermissionAllowMode                       m_allowMode = PERMISSION_RULE_ALLOW_MODE_ASK;
    SP<CAsyncDialogBox>                               m_dialogBox;                  // for pending
    SP<CPromise<std::string>>                         m_promise;                    // for pending
    SP<CPromiseResolver<eDynamicPermissionAllowMode>> m_promiseResolverForExternal; // for external promise

    SDynamicPermissionRuleDestroyWrapper              m_destroyWrapper;

    friend class CDynamicPermissionManager;
};

class CDynamicPermissionManager {
  public:
    void clearConfigPermissions();
    void addConfigPermissionRule(const std::string& binaryPath, eDynamicPermissionType type, eDynamicPermissionAllowMode mode);

    // if the rule is "ask", or missing, will pop up a dialog and return false until the user agrees.
    // (will continue returning false if the user does not agree, of course.)
    eDynamicPermissionAllowMode clientPermissionMode(wl_client* client, eDynamicPermissionType permission);

    // for plugins for now. Pid 0 means unknown
    eDynamicPermissionAllowMode clientPermissionModeWithString(pid_t pid, const std::string& str, eDynamicPermissionType permission);

    // get a promise for the result. Returns null if there already was one requested for the client.
    // Returns null if state is not pending
    SP<CPromise<eDynamicPermissionAllowMode>> promiseFor(wl_client* client, eDynamicPermissionType permission);
    SP<CPromise<eDynamicPermissionAllowMode>> promiseFor(const std::string& str, eDynamicPermissionType permission);
    SP<CPromise<eDynamicPermissionAllowMode>> promiseFor(pid_t pid, const std::string& key, eDynamicPermissionType permission);

    void                                      removeRulesForClient(wl_client* client);

  private:
    void askForPermission(wl_client* client, const std::string& binaryName, eDynamicPermissionType type, pid_t pid = 0);

    //
    std::vector<SP<CDynamicPermissionRule>> m_rules;
};

inline UP<CDynamicPermissionManager> g_pDynamicPermissionManager;