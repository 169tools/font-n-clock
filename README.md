# フォントな時計 / Font-n-Clock

ローカルフォントを使用して時計素材を作成できる OBS プラグイン
https://obsproject.com/forum/resources/2680

## 環境構築

```zsh
$ brew install cmake ninja xwin obsproject/tools/gersemi obsproject/tools/clang-format@19
$ xwin --accept-license --arch x86_64 --output ~/.xwin
$ ln -s ~/.xwin .xwin
$ make configure
$ make index
$ make build
$ make link	# OBS を再起動で build 後の更新が反映されるようにする
```

## コード署名ポリシー

[CODE_SIGN|NG_POLICY.md](./CODE_SIGN|NG_POLICY.md)
