# 插件

截图吧通过 `.jtbplugin` ZIP 包安装插件。系统托盘右键菜单或剪贴板窗口右下角菜单中的“安装插件”会检查清单和包内路径、限制解压大小，并将插件安装到数据目录的 `plugins/<id>/`。同 ID 的安装包会更新现有插件，并保留启用状态；“管理插件”可以启用、停用或卸载插件。插件是本机可执行程序，具有当前用户权限，安装前请确认来源可信。

插件项目和示例位于 [jietuba-plugins](https://github.com/adoin/jietuba-plugins)。发布页提供已构建的 `.jtbplugin` 文件。插件单独发布，不依赖截图吧的 Qt 版本。

## 清单与执行

ZIP 根目录包含 `plugin.json`，其中 `id` 由小写字母、数字、点、下划线或短横线组成；`api_version` 为 `2`，`type` 为 `extension`。插件在 `contributes` 下组合声明命令、钩子、工具栏和剪贴板能力。`command` 是包内相对路径的 Windows `.exe`；仅提供声明式内容的插件可省略。进程插件从标准输入读取 UTF-8 JSON，从标准输出写回一个 JSON 对象，不得在标准输出写日志。旧协议插件包不受支持，需要使用新协议重新打包。

## 程序级扩展：事件、命令与工具栏

`type: "extension"` 使用 `contributes` 声明命令、事件钩子、工具栏栏位及剪贴板能力；各项可以在同一个包中组合。当前宿主接入截图与剪贴板域，其他域可使用相同协议扩展。

```json
{
  "id": "my-extension", "name": "Example", "version": "1.0.0",
  "api_version": 2, "type": "extension", "command": "my-extension.exe",
  "contributes": {
    "commands": ["convert"],
    "hooks": [{"event": "screenshot.before_export", "priority": 10}],
    "toolbar": [{
      "id": "convert", "location": "screenshot", "name": "Convert and copy",
      "name_i18n": {"zh": "转换并复制"},
      "pipeline": [{"plugin": "my-extension", "command": "convert"}]
    }]
  }
}
```

截图工具栏栏位默认位于“…”中，可在工具栏的调整界面显示、隐藏或排序。`icon` 可选，填写包内 PNG 或 SVG 相对路径；未提供时使用通用图标。`pipeline` 按数组顺序调用不同插件声明的 `commands`，前一步的结果就是下一步的输入；缺少或停用依赖、执行失败、响应无效和超时都会中止，保留截图窗口供重试。末尾结果为图片或文本时复制到剪贴板。工具栏栏位在下一次截图时重新加载；钩子和命令以执行时的启用状态为准。

当前截图事件依次包括 `screenshot.started`（开始截图会话）、`screenshot.selection_confirmed`（选区确认，可多次触发）、`screenshot.before_export`（复制、保存、快速保存、钉图或插件栏位导出前）以及 `screenshot.exported`（生成最终输出）和 `screenshot.ended`（会话结束）。没有导出就结束时还发送 `screenshot.cancelled`。`exported` 表示输出已生成，并不保证随后异步复制或保存成功；OCR、扫码、长截图和录屏暂不经过导出链。钉图插件改变图片时使用合成结果，标注不再可单独编辑。

同一截图会话的事件和命令串行执行；同一事件的钩子按 `priority` 从大到小排序，相同优先级按插件 ID 排序。`before_export` 钩子接收 PNG 图片并可返回修改后的图片；失败时取消该次导出，不会静默绕过处理。其他事件用于通知，返回值不会改变后续的原始选区。事件和命令在后台进程执行，启动超时 2 秒、运行超时 5 秒；关闭截图窗口后不再执行与该窗口关联的完成回调。

请求是 `{"api_version":2,"type":"extension","event":"...","command":"...","payload":{...},"context":{...}}`。事件的 `command` 为空，工具栏命令的 `event` 为 `command`。`context` 包含 `session_id`、`action`、选区坐标 `selection`；截图内容为敏感信息，只有声明了相应事件或命令的已启用插件才会收到。插件返回 `{"api_version":2,"ok":true,"payload":{...}}`；省略 `payload` 表示保持输入，`ok:false` 表示失败。截图链的数据类型为 `{"kind":"image","data":"<PNG Base64>"}`、`{"kind":"text","data":"..."}` 或 `{"kind":"none"}`；总 JSON 上限 8 MiB，图片不超过 6400 万像素。导出前图片超过限制时中止导出；导出通知若图片超过限制则只携带 `none`。

## 剪贴板能力

在 `contributes.clipboard` 中可组合声明 `preview`、`action`、`metadata` 和 `backup`。前三者声明 `content_types`，命令型能力的 `command` 必须在 `contributes.commands` 注册。预览命令接收 `payload.kind: "clipboard"`，`data` 中包含 `content_type`、`sub_kind`、`content`、`language`、`paths` 和可选 `html`。清单中的 `formats` 最多声明 8 种原始格式，符合限制的数据通过 `formats_base64` 提供。预览返回 `payload: {"kind":"preview","data":{"kind":"text|html|markdown|image","content":"..."}}`；不适用时返回 `payload: {"kind":"none"}`。预览失败、无效或超时后回退到内置预览。

文本操作命令接收 `payload: {"kind":"text","data":"..."}`，返回相同类型；`context.source_app` 提供来源。这个通用文本命令也可以接在截图插件的文本输出之后。格式检查使用预览能力的 `modes: ["formats"]` 和 `formats: ["*"]`；选择单项时提供该格式的数据。

采集过滤器在 `contributes.hooks` 中声明 `clipboard.before_capture` 和 `content_types`。事件的 `payload.kind` 为 `clipboard`，返回 `payload: {"kind":"decision","data":false}` 可阻止入史。过滤器出错、超时或输入过大时匹配内容不会保存，并提示用户修复或停用插件；安装、更新和停用与采集互斥。

清单中的 `settings` 可声明最多 8 个 `multiline` 设置项，字段包括 `key`、`type`、`label` 和可选的 `label_i18n`、`default`。用户在“管理插件 → 插件设置”编辑；插件通过环境变量 `JIETUBA_PLUGIN_CONFIG` 读取 JSON 文件。配置独立于插件程序保存，更新或卸载插件时保留。

“敏感内容保护”过滤器识别部分高置信度私钥、服务令牌和 JWT；它不能识别所有秘密，且可能跳过文档中的样例。“忽略指定应用”过滤器按设置中的程序文件名精确匹配来源，系统无法确定来源时无法匹配。

安装具有 `clipboard.backup` 能力的扩展后，剪贴板设置菜单提供加密备份及恢复。备份命令接收 `payload.kind: "backup"`，`data` 包含操作、路径和本次密码。备份使用 SQLite 一致快照与图片文件，恢复先验证到新目录，再可选择切换；现有数据不被覆盖。密码只在本次任务中通过标准输入传给插件，不保存在插件设置中。

声明式 `clipboard.metadata` 能力使用 `fields` 描述条目级 `line` 或 `multiline` 字段。用户通过条目右键菜单编辑，内容存入历史数据库并参与搜索；删除条目时元数据随之删除，也包含在完整备份中。插件被停用或卸载时，已保存的条目元数据保留。

## 原始格式

截图吧保存常见文本、HTML、RTF、图片、文件及额外注册格式，格式视图可按名称逐项查看。额外格式最多采集 16 项、单项 4 MiB、合计 16 MiB。虚拟文件、OLE 句柄、隐私标记和可能触发昂贵延迟渲染的表格格式不作为通用格式采集；已有普通文本、图片及文件行为保持不变。来源应用要求不记录的内容仍会跳过。插件只能读取已经保存且自己在清单中声明的格式。
