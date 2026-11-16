# Changelog —— WhoLock

## v1.0.0 (2026-11-16)
- **多目标扫描**：目录递归展开（深度 8 / 上限 2000 / 重解析目录跳过）、CLI 多路径、GUI 多行+多文件拖入；新增「占用文件」列精确归属（同进程多文件合并计数）
- **身份等价类**：FileIdInfo（卷序列号+文件ID64）比对，消除 8.3/硬链接/软链字符串误差；卷 GUID→盘符反查实装；G6 硬链接判据（锁链接路径→按原名命中）
- **深度扫描引擎（F6）**：子进程隔离全表句柄枚举（`--internal-htfull` 协议、stdin 传路径、逐行 flush、父进程超时杀+部分结果回收）；GUI「深度扫描」复选框（settings 持久化）、CLI `--engine=deep`；fork 炸弹熔断（internal 进程禁止自举）+ spawn 目标 exe 显式指定（自举陷阱销案）
- **句柄详情面板（F7）**：选中进程→子进程拉取句柄明细（HandleValue/GrantedAccess/类型/路径），挂死隔离同上
- **远端句柄释放（F8）**：DuplicateHandle(DUP_CLOSE_SOURCE) 不杀进程释放占用；危险模式+逐句柄确认
- **释放后删除闭环（F11）**：结束进程/释放句柄成功后询问删除；DeleteFileW + GetFileAttributes 双验证
- **应用图标（F9）**：程序化扁平多尺寸 ICO（品牌蓝+白"锁"字，无投影），rc 资源嵌入单 exe
- **版本单一事实源（F2）**：CMake configure_file → version.h；标题栏/品牌/CLI `--version`/README 四处同源 + G8 机检
- **设置持久化（F3）**：窗口几何+引擎偏好（%APPDATA%\WhoLock\settings.ini）；危险模式勾选接口层杜绝持久化
- **门禁扩展**：check.ps1 8 项（新增图标机检、G8 版本一致性）；ctest 7 套件（+identity/settings/dir/release）
- **Explorer 右键集成（F12）**：HKCU 当前用户注册表（可逆、免管理员）；文件+目录右键「查找占用」→ GUI 自动扫描；软件内开关以注册表实况为准，exe 挪位后重勾即修复指向；`command` 子键分离实现
- **状态带四行**：耗时/引擎明细/深扫与右键提示
- 构建测试规模：零警告 /W4、ctest 8/8（+shell HKCU 真跑往返）、dumpbin 白名单、依赖单向机检

## v0.2.0 (2026-11-16)
- UI 全量视觉升级：Theme 令牌单一事实源、flat owner-draw 按钮三 tone、品牌字标带、1pt hairline、ListView 全行 customdraw（交替行/选中蓝底/34px 行高/垂直居中/路径省略号/空态文案）、状态带父窗直绘三色、拖放/浏览/列排序、manifest v6+PerMonitorV2
- 截图取证 7 轮迭代；新坑 7 条（COMPOSITED 冲突/manifest 回落/GetSubItemRect 不可靠等）

## v0.1.0 (2026-11-16)
- 首版：RestartManager + 句柄表（候选门防挂死）双引擎、G1 自锁判据、结束进程三重护栏、CLI --cli/--json、单 exe 免装（/MT 静态）、check.ps1 六项门禁
