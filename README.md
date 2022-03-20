![MiniVNC for Macintosh][mac-screenshot]

MiniVNC Remote Desktop Server for Vintage Macintosh Computers
=============================================================

MiniVNC is a remote desktop server that has been written from the
ground up for best performance on 68k Macintosh computers.

It was originally an experiment to see whether a Macintosh Plus could
be controlled remotely, but has since been expanded to support the
Apple Lisa and also all vintage color Macs! :rainbow:

[![MiniVNC on a Macintosh LC II](https://github.com/marciot/mac-minivnc/raw/main/images/youtube2.png)](https://youtu.be/_o8JiXqFHsk)

- [Video 1](https://youtu.be/zM_sNItbuhc) - Running on a Macintosh Plus in B&W
- [Video 2](https://youtu.be/_o8JiXqFHsk) - Running on a Macintosh LC II in Color
- [Video 3](https://youtu.be/BJqkBN8TZkc) - Apple Lisa Chat and Remote Desktop Demo

Compatibility
-------------

MiniVNC is built on MacTCP and requires System 7, but it will
operate on later Macs using Open Transport. MiniVNC has been
developed and tested using a [RaSCSI device] operating as an
Ethernet bridge, but should also work using a Mac with a built-in
Ethernet port.

Sponsorship Perks
-----------------

As a special reward to [GitHub sponsor] at the "Sustaining Supporter" tier or above, I am providing a 🎁 **bonus archive** in the releases page with additional perks. These perks are:

- **RaSCSI System 7.0.1 Boot Image:** This pre-made disk image includes a complete installation of System 7.0.1 pre-configured with MacTCP, DaynaPort SCSI/Link drivers and MiniVNC running in headless mode, which allows RaSCSI users to boot and remotely control a Macintosh with _zero configuration_ and even _without an ADB mouse and keyboard_. This disk image can also be mounted and changed in Basillisk II.
- **Headless Server Mode:** When placed in the "Startup Items", the Sponsors Edition of MiniVNC automatically starts listening for connections at system boot, allowing it to be used on Macs without a keyboard, mouse, or monitor.
- **Different Compression Levels:** These custom builds allow experimentation with different compression modes.

Please refer to your sponsorship welcome letter or GitHub sponsor dashboard for the archive password.

How can you help?
-----------------

You can help this project in one of the following ways:

* :sparkles: Star this project on GitHub to show your support!
* :loudspeaker: Subscribe to my [YouTube channel]!
* :raising_hand: Download the binaries from the [releases page] and participate in the [beta test] discussion!
* :heart: Become a [GitHub sponsor] to help fund my open-source projects!

[beta test]: https://github.com/marciot/mac-minivnc/discussions/1
[releases page]: https://github.com/marciot/mac-minivnc/releases
[GitHub sponsor]: https://github.com/sponsors/marciot
[YouTube channel]: https://www.youtube.com/channel/UC1tZ8uA0WJp5pDpPwldQ0Ig

Project Goal
------------

The goal of MiniVNC is to provide better performance and compatibility
with older Macs than [ChromiVNC]

It accomplishes these goals by:

- Using Classic Networking (i.e. [MacTCP]) rather than Open Transport
- Implementing a limited subset of the [Remote Framebuffer Protocol],
which favor of performance over full compatibility with all clients.
- Implements fast but inexact screen change detection, favoring
performance and low memory utilization while allowing for the occasional
missed update and visual artifacts.
- Written in C++, with optimized 68x assembly code where necessary for best
performance.

[MacTCP]: https://en.wikipedia.org/wiki/MacTCP
[ChromiVNC]: https://web.archive.org/web/20070208223046/http://www.chromatix.uklinux.net/vnc/index.html
[Remote Framebuffer Protocol]: https://datatracker.ietf.org/doc/html/rfc6143
[RaSCSI device]: https://github.com/akuker/RASCSI
[mac-screenshot]: https://github.com/marciot/mac-minivnc/raw/main/images/screenshot.png "MiniVNC Screenshot"

More Software for Vintage Macs
------------------------------

* [Trouble in Paradise for Macintosh]: An Iomega Zip and Jaz cartridge tester for vintage Macs! 
* [ScreenChooser]: A dynamic background changer for vintage Macs!
* [Retroweb Vintage Computer Museum]: A web-based museum of vintage computers, including the Macintosh!

[ScreenChooser]: https://archive.org/details/screen-chooser
[Trouble in Paradise for Macintosh]: https://github.com/marciot/mac-tip
[Retroweb Vintage Computer Museum]: http://retroweb.maclab.org

Technical Notes
---------------

Development of MiniVNC required months of experimentation and hacking.
The following write-up provides details for developers who have an
interest.

<details>
<summary>
Click here to read this section
</summary><br>
  
#### MacTCP Programming is Hard; Doing it Efficiently is Harder
  
MacTCP is Apple's first TCP/IP networking stack and is the only
networking API available on the Macintosh Plus. The
[MacTCP Programmer's Guide] is a good resource, but lacks code
samples and makes no mention of high-level languages. The
[MacTCP Cookbook] article by Steve Falkenburg provides more meat
to chew on but actual source code is worth a thousand words. I
finally it on the [Apple Developer Group CD Volume VII] in the
form of a "finger" protocol example in the directory
`Dev.CD Vol. VII:develop:develop 6 code:TCP:finger`

This is a good starting point as the "TCPRoutines.c" file
provides an example of using MacTCP parameter blocks from a
high-level language. Steve's helper routines, while easy-to-use
for simple tasks, are very slow. As I later learned, the most
efficient way to do MacTCP programming is via asynchronous callback
routines.

Since these callback routines execute in interrupt time, writing them
in a high-level language is challenging. I used the technique from the
article "Asynchronous Routines on the Macintosh" in [Develop magazine],
March 1993 which involves an assembly language glue routine. Later on,
I used a similar routine for making a Vertical Retrace task for gathering
screen updates.

Today we take for granted threads which make it easy to implement
network applications. The use of callback routines is a huge step
backwards.

For one, it precludes using temporary stack-based storage and instead
all state must be stored in global variables. Doing things like loops
are trivial in a thread but very difficult using callback routines.
  
The VNC protocol is fairly simple, but the code is far more
complicated than it would have been had I modern techniques at my
disposal.

As an aside, Ari Halberstadt wrote a very promissing [thread library]
for the Macintosh. Getting it to work with MacTCP might have simplified
the programing model, but at the time it was too much of a heavy lift
for me to get it to work while I was also learning MacTCP. I ended up
using some of his basic OS utilities code in MiniVNC, but not the thread
library itself.

[MacTCP Programmer's Guide]: https://github.com/marciot/mac-minivnc/raw/main/docs/MacTCP_programming.pdf
[MacTCP Cookbook]: http://preserve.mactech.com/articles/develop/issue_06/p46-69_Falkenburg_text_.html
[Apple Developer Group CD Volume VII]: https://archive.org/details/apple-developer-group-cd-series-volume-vii-lord-of-the-files-1991-cd-rom
[Develop magazine]: https://vintageapple.org/develop/
[thread library]: https://web.archive.org/web/20211216043914/http://websites.umich.edu/~archive/mac/development/source/threadlib1.0d4.cpt.hqx

#### Mouse Control

On the Macintosh, the mouse can be progmattically changed by writing
the new position to the low-memory globals `MouseTemp` and
`RawMouseLocation` and then copying the value of `CrsrCouple` to
`CursorNew` to signal the change. This technique is borrowed from
ChromiVNC.

Mouse button control presented a challenge. The technique used by
ChromiVNC is to write the new mouse button state to the low-memory
global `MBState` while simultaneously posting a `mouseUp` or `mouseDown`
event to the System event queue. This works on modern Macs, but on the
Macintosh Plus it only works for clicking. Mouse dragging &mdash; and
crucially, menu selection &mdash; does not work. On that machine the
VIA interrupt in ROM constantly overwrites `MBState` with the button
state from the physical mouse, so the trick of writing a value to
`MBState` does not work.

I attempted patching the `Button` trap and implemented a journaling driver,
but the former was ineffective while the latter was found to be broken and
unusable under System 7's multi-tasking model.

At last, an analysis of the disassembled code for the VIA interrupt
revealed it deglitched the mouse button by waiting three ticks prior to
updating `MBState`. The low-memory variable `MBTicks` indicates the start
of the wait period and by periodically setting it to the future &emdash;
in advance of `TickCount` &mdash; I found I could keep the VIA routine
waiting indefinitely so that I could alter `MBState` at will. This
allowed me control of the mouse button on all Macs, including the Macintosh
Plus.

#### Screen Change Detection via Checksums

On the vintage Macintosh, the address of the framebuffer is stored in the
low-memory global `ScrnBase`. A crucial part of a VNC server is detecting
changes to the screen. While this could be done by maintaining a separate
copy of the framebuffer and comparing every pixel, this requires a lot of
memory and a lot of memory accesses.

In MiniVNC, I decided to compute a 32-bit sum across each row of pixels
and a 32-bit sum up and down the screen. For both the horizontal and
vertical sums, the new and old sums are compared to detect screen changes.
It turns out this is also an quick way to determine the bounds of the change
rectangle as the location of the first and last changed sum along an axis
can be taken as the rectangle bounds along the axis.

This is an inexact approach which makes the server blind to many types of
screen changes. Using a [position-dependent checksum] like a [Fletcher's checksum]
would improve accuracy, at the expense of more computation and storage.

#### Byte Alignment and Reduction of Horizontal Resolution of Change Rectangles

An ordinary VNC server would attempt to minimize transmission by sending
the smallest possible change rectangle. MiniVNC keeps the horizonal
bounds of change rectangles aligned to byte boundaries. On the Macintosh
Plus, which uses one bit per pixel, this is crucial as it allows screen
data to be sent without any bit-shift or bit masking operations, which
are particularly slow on the 68000.

When using a 32-bit column sum (as opposed to the XOR operation used in
my first implementation), a change in one pixel column can cause a carry
to the next, meaning that the horizontal bounds of change rectangles are
further aligned to 32-bit boundaries.

On a B&W display, this causes change rectangle bounds to fall on 32 pixel
increments, which might cause quite a lot of extra data to be transmitted.

The effect is minimized on color displays, which pack fewer pixels per byte.
For example, on a 256 color display, the change rectangles can fall on 4
pixel boundaries, mimimizing the size of change rectangles.

#### Use of TRLE Encoding to Avoid Bit Unpacking and Packing

The VNC protocol is meant for color computers and most encodings
assume a color depth of at least eight bits. However, the TRLE
encoding is unique in that it supports a paletted tile type that
allows for a 1-bit, 2-bit and 4-bit encodings.

The ability to transmit 1-bit data is what caused me to require
MiniVNC to use TRLE encoding exclusively. This does not meet the
VNC specifications, but is compatible with modern clients. To
support raw encoding, as required by the specifications, would
necessitate expanding each 1-bit pixel in a byte into a full
color byte, an operation which would be prohibitive on older
Macs.

On the Macintosh Plus, I transmit all tiles using the 1-bit
paletted tile type. This, together with byte alignment, allows
the encoding process to be a straight copy full bytes without
any bit shifting or masking.

As an optimization, during the copy I will detect if a tile
consists of all zeros (i.e. white). If this is the case, I emit
a solid white tile. There is no corresponding case for black
pixels, so a white region will compress better than a black
region.

On color Macintosh computers, where I have the luxury of a
68020 with an instruction and data cache, I perform additional
computation in order to choose from among the following TRLE
tile types:

- Solid tiles
- Paletted 1-bit tiles
- Paletted 2-bit tiles
- Raw 8-bit tiles
- RLE encoded tiles

On a color Mac, the full encoding process would work like this:

1. The tile is encoded as a plain RLE tile. During this stage,
up to five unique colors from the tile are recorded.
2. If the number of unique colors is equal to one, a solid tile
is emitted
3. If the number of unique colors is equal to two and the size
of the RLE encoded tile exceeds that of a paletted 1-bit tile,
then a 1-bit paletted tile is emitted
4. If the number of unique colors is equal to three or four and
the size of the RLE encoded tile exceeds that of a paletted 2-bit
tile, then a 2-bit paletted tile is emitted
5. If the number of unique colors exceeds fours and the size of
the RLE encoded tile exceeds that of a raw 8-bit tile, a raw 8-bit
tile is emitted.

In practice, I found that doing this whole process actually hurt
performance on an Macintosh LC II, so MiniVNC supports different
packing levels which omit many of the steps listed above.

For example, packing level 2 looks like this:

1. The tile is encoded as a plain RLE tile.
2. A solid tile is emitted if only one run is found.
3. If the RLE encoded tile exceeds the size of a paletted tile
of the same color depth as the screen, emit that tile instead.
  
The key different between packing 2 and the full process is that
I do not emit a tile with fewer colors than the color depth of the
screen itself. Doing so requires finding the unique colors in
a tile and mapping those to a smaller palette, which is expensive.
Emitting a tile with the *same* number of colors as the screen,
however, is trivial as it only involves a straight copy with no
changes to the color values themselves.

#### CodeWarrior or Symantec C++

There is not, unfortunately, a perfect development environment for
the Macintosh when it comes to writing code that mixes C++ and
assembly language. My go to language used to be Symantec C++ 7
because it allows you to write C++ routines and add assembly
language bits and pieces just where you need it, minimizing the
learning curve. It is also very good for things like code resources
or device drivers.
  
CodeWarrior 8, on the other hand, works great on Basillisk II and
has a better IDE. I've lately begun using it over Symantec C++ 7
for these reasons, but one frustrating aspect is that it does not
allow you to mix C and assembly language in one function. Instead,
you must chose one or the other. This meant that in MiniVNC I had
to write whole functions in assembly language, which meant doing
things like pulling arguments off the stack myself, something
Symantec C++ 7 would do for me.

#### Assembly Language Tricks for Performance

The TRLE encoder was written in 68x assembly for best performance.
For the color encoders, a 68020 is assumed and the code makes special
use of 68020 instructions such as `bfextu`. Most functions take
advantage of as many of the available eight data and eight address
registers as possible, as to minimize memory accesses.

One example is the color palette gathering code. In the RLE encoding
routine, use two register halves of two registers to keep track of
colors I have seen before, which allows me to generate a palette of
up to four colors without having to write to memory. I implemented
another routine which uses eight 32-bit registers as bitfields to
tally up to 256 unique colors&mdash;again, with no memory access.
This routine would be needed for generating 16 bit color tiles
when the screen is in 256 color mode, although I have not actually
implemented this at this point (and probably never will, as it appears
that even generating 2 or 4 color tiles is too slow to be worth the
effort).

</details>

[Fletcher's checksum]: https://en.wikipedia.org/wiki/Fletcher%27s_checksum
