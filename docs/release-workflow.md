# 发布与上游同步

`origin` 是自己的仓库，`upstream` 是官方仓库。保持 `master2` 用于跟进官方，`feat/plugin-ecosystem` 保留自己的插件和品牌改动。发布工作流从标签指向的提交读取，不依赖分支名称，因此要对包含该工作流、品牌配置和所需功能的提交打标签。官方纯净分支没有这些修改时，对其打标签不会自动变成个人版本。

先提交或妥善保存当前未提交的工作，再同步官方分支：

```powershell
git fetch upstream --no-tags
git switch master2
git merge --ff-only upstream/master2
git push origin master2
git switch feat/plugin-ecosystem
git merge upstream/master2
```

个人分支若与官方更新冲突，解决冲突并完成验证后再提交。`--ff-only` 不会覆盖或重写自己的分支历史；若它失败，先检查分歧，不要强推。官方标签不自动取回，也不使用 `git push --tags` 批量推送，避免意外触发发布。

主程序使用 `release-<VERSION>`，其中版本必须与仓库的 `VERSION` 文件一致。发布前先将 `VERSION` 改为未使用的新版本并提交；例如改为 `2.2.4` 后：

```powershell
git tag release-2.2.4
git push origin release-2.2.4
```

推送该标签后，工作流构建并发布对应提交的版本，生成 x64 和 ARM64 完整版安装包、轻量版和完整版便携 ZIP、校验值及 WinGet 清单；普通分支推送不发布。安装包在各自架构的运行器上完成安装、升级和卸载检查后才会发布。默认按当前用户安装，可选择安装路径；卸载保留设置、模型和其他用户文件。运行中的程序需要先退出才能升级或卸载。

安装包与程序共用 `branding.json` 和 `VERSION`。如果启用 SignPath 签名，还需配置 `SIGNPATH_INSTALLER_ARTIFACT_CONFIGURATION` 指向安装包对应的签名配置；不配置签名策略时生成的安装包未签名。

不要覆盖、移动或强推已有标签。单独的 `lite.yml` 可手动构建测试制品，但不创建 Release。插件仓库使用 `v<版本>` 标签，详见插件仓库文档。
