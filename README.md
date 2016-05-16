# GenSysTray - version 1.0

A configurable system tray icon writen in C.

##LICENSE - GPLv3 no later option. Ignore any references to later versions
See LICENSE to know your rights or go to
http://www.gnu.org/licenses/gpl-3.0.txt

###How to configure GenSysTray ?

open .config/gensystray/gensystray.config with your favorite
text editor:

```
@/full/path/to/the/16x16/icon.png

[name of the button]
command to execute on button click

[name of another button]
command to execute on button click

all the rest is ignore except text between square
brackets and text in the next line right after
```

FAQ:
- Why no later option ?
Because I can't agree with a license that doesn't exist yet.
Conceding such rights away would be irresponsible.

- Why a Generic System Tray Icon ?
Sometimes you execute some scripts so often, that you would
rather see them automated and at distance of a single click.

- Is there a way to change the default location of the config file ?
Yes! You can set the environment variable GENSYSTRAY_PATH
to your custom config file

###Instructions to compile
install libgtk-3-dev libsdl2-2.0-2-dev gcc
run build_gcc.sh


###How it looks like
[![GenSysTray](http://i2.ytimg.com/vi/Ip3fAB-YqTg/0.jpg)](https://www.youtube.com/watch?v=Ip3fAB-YqTg)
