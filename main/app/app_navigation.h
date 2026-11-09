/*
 * 中文：主页面路由、同步切页与一次性返回来源；主循环保留输入取消和切页门控。
 * English: Root routing, synchronous page changes and one-shot return origins; the loop retains input cancellation and transition gates.
 */
#pragma once
#include "app.h"

typedef struct {
    const app_desc_t *return_app; ///< 返回来源页 / Return origin
    int return_leaf; ///< 来源页内页码 / Origin page leaf
    int return_menu_leaf; ///< 来源菜单页码 / Origin menu leaf
    bool return_to_menu; ///< 返回时恢复菜单 / Restore the origin menu
} app_navigation_history_t;

typedef struct {
    const app_desc_t *next; ///< 本轮目标页 / Requested destination
    bool go_back; ///< 返回优先于普通请求 / Return takes precedence
    bool selected_from_menu; ///< 本轮请求来自菜单 / Request originated in the menu
} app_navigation_request_t;

/// 菜单仍由主循环原入口同步绘制与呈现，menu_context 仅借用到调用结束。
/// The loop synchronously paints and presents menus through its original entry; menu_context is borrowed only for the call.
typedef void (*app_navigation_menu_present_t)(app_ctx_t *ctx, const app_desc_t *current,
                                             int leaf, void *menu_context);

/// 只认四个已注册主页面，并尊重页面当前是否显示主视图。
/// Accept only the four registered roots whose main view is currently visible.
bool app_navigation_main_page_ready(app_ctx_t *ctx, const app_desc_t *app);
/// 保留主页面接管、左右循环和中键长按刷新规则。
/// Retain root overrides, side-key cycling and middle-hold refresh rules.
app_redraw_t app_navigation_main_key_action(app_ctx_t *ctx, const app_desc_t *current,
                                           int key, bool held);
/// 启动时清除导航接触锁存；不会清除正文返回快照。
/// Clear navigation-contact latches at loop start, retaining reader-return snapshots.
void app_navigation_reset_contact(void);
/// 读错或释放时清除接触锁存，调用位置由主循环保持。
/// Clear contact latches on read failure or release at the loop's existing ingestion point.
void app_navigation_observe_contact(esp_err_t error, bool touched);
/// 书架阻塞轮询是否已接管本次接触。/ Whether shelf polling already owns this contact.
bool app_navigation_contact_polled(void);
/// 主循环已在首次单指报点时接管底栏。/ The loop claimed the root bar on initial single contact.
void app_navigation_claim_contact(void);
/// 快捷层离开正文前保存原有书名路径和全屏快照。
/// Capture the existing book path and fullscreen snapshot before a quick-layer departure from the body.
void app_navigation_capture_reader_return(const app_desc_t *current);
/// 主循环完成请求快照、输入取消与门控后调用；同步修改页面/菜单和返回来源。
/// Called after the loop snapshots requests, cancels input and applies gates; synchronously updates page/menu state and return history.
/// 保持 on_exit、字体激活、正文恢复、on_enter、页码恢复和呈现的原有顺序。
/// Retain the ordering of on_exit, font activation, body restoration, on_enter, leaf restoration and presentation.
void app_navigation_apply_request(app_ctx_t *ctx, const app_desc_t **current,
                                   bool *menu_open, int *menu_leaf,
                                   app_navigation_history_t *history,
                                   app_navigation_request_t request,
                                   app_navigation_menu_present_t present_menu,
                                   void *menu_context);
