# 品牌与独立发布

项目根目录的 [`branding.json`](../branding.json) 是品牌配置的唯一来源：

- `name`：小写字母和数字组成的可执行文件及发布包前缀。
- `display_name`：程序界面和 Windows 文件属性显示的名称。
- `display_name_zh`：中文界面显示的名称。
- `repository`：独立发布使用的 GitHub 仓库，格式为 `所有者/仓库名`。

修改配置后重新构建。开发程序、轻量版、完整版和更新辅助程序分别使用 `<name>_cpp.exe`、`<name>_lite.exe`、`<name>_pp.exe` 和 `<name>_updater.exe`；发布 ZIP、更新器匹配规则和生成的 WinGet 清单使用同一前缀。更新检查只查询配置中的仓库；该仓库尚无正式 Release 时，更新检查无法获取版本，不会转到上游仓库。

设置、插件和默认剪贴板数据库使用 `%APPDATA%\<display_name>`，日志与本地缓存使用 `%LOCALAPPDATA%\<display_name>`，注册表和启动项也使用品牌名称。便携模式仍使用程序旁的 `settings.ini` 和插件目录。

独立版本不自动导入旧产品的数据；旧目录及注册表不会被删除。插件扩展协议和源代码内部标识独立于产品品牌。截图保存目录及更新恢复目录使用品牌前缀。产品图标由根目录的 `app.ico` 统一提供，版权声明保持原样。

发布工作流只在推送 `release-<VERSION>` 标签时创建正式 Release，不会因提交、推送分支或修改品牌配置而发布。若配置 `SIGNPATH_SIGNING_POLICY` 开启签名，还需在仓库或 `signing` 环境配置自己的 `SIGNPATH_PROJECT_SLUG`、组织 ID 和 API token；可用 `SIGNPATH_ARTIFACT_CONFIGURATION` 指定自己的制品配置。不设置签名策略时生成未签名包。官方同步与打标签方式见[发布与上游同步](release-workflow.md)。
