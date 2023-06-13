# ColumnMode

ColumnMode is a lightweight, column-based text editor. 

![Example image](https://raw.githubusercontent.com/clandrew/ColumnMode/master/Images/Sign.gif "Example image")

**Features:**

* Allows column-based selections, e.g., rectangular blocks of text, as opposed to lines.
* Suitable for ASCII-based charts and artwork
* Blocks of text can be moved with SHIFT+arrowkeys keystroke
* Supports copying and pasting
* Supports undo
* Supports print
* Supports extension via plugins
* Supports [Themes](Manual/Themes.md) to change the look and feel

![Example image](https://raw.githubusercontent.com/clandrew/ColumnMode/master/Images/CutPaste.gif "Example image")
![Example image](https://raw.githubusercontent.com/clandrew/ColumnMode/master/Images/Undo.gif "Example image")

The motivation for creating this was because of difficulties finding a text editor that supported moving around rectangular regions of text. Not that I looked that hard, but most text editors have selections based on lines, not rectangular regions delineated by columns.

**Setup**

The release is a standalone executable, no installer.

**Usage Environment**

This program is for Windows 7/Windows 10+ x86-compatible environments.

**Build Notes**

This program was built using Microsoft Visual Studio 2017 version 15.7.5. It's written in C++. The window-based functionalities are set up using plain Win32. For graphics and text manipulation it uses Direct2D and DirectWrite. 
