# フォントな時計 / Font-n-Clock

ローカルフォントを使用して時計素材を作成できる OBS プラグイン<br>
https://obsproject.com/forum/resources/2680

## 環境構築 / Environment setup

```zsh
$ brew install cmake ninja xwin obsproject/tools/gersemi obsproject/tools/clang-format@19
$ xwin --accept-license --arch x86_64 --output ~/.xwin
$ ln -s ~/.xwin .xwin
$ make configure
$ make index
$ make build
$ make link	# OBS を再起動で build 後の更新が反映されるようにする
```

## コード署名ポリシー / Code signing policy

For the Windows binary, Free code signing provided by [SignPath.io](https://signpath.io/), certificet by [SignPath Foundation](https://signpath.org/).

This project is maintained by a single developer, mizznoff, who acts as the sole commiter, reviewer, and approver.

This program will not transfer any information to other networked systems unless specifically requested by the user or the person installing or operating it.
