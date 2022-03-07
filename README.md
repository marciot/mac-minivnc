![MiniVNC for Macintosh][mac-screenshot]

MiniVNC Remote Desktop Server for Vintage Macintosh Computers
=============================================================

MiniVNC is a remote desktop server that has been written from the
ground up for best performance on 68k Macintosh computers.

It was originally an experiment to see whether a Macintosh Plus could
be controlled remotely, but has since been expanded to support color
on all vintage color Macs! :rainbow:

[![MiniVNC on a Macintosh LC II](https://github.com/marciot/mac-minivnc/raw/main/images/youtube2.png)](https://youtu.be/_o8JiXqFHsk)

- [Video 1](https://youtu.be/zM_sNItbuhc) - Running on a Macintosh Plus in B&W
- [Video 2](https://youtu.be/_o8JiXqFHsk) - Running on a Macintosh LC II in Color

Compatibility
-------------

MiniVNC is built on MacTCP and requires System 7, but it will
operate on later Macs using Open Transport. MiniVNC has been
developed and tested using a [RaSCSI device] operating as an
Ethernet bridge, but should also work using a Mac with a built-in
Ethernet port.

How can you help?
-----------------

You can help this project in one of the following ways:

* :sparkles: Star this project on GitHub to show your support!
* :raising_hand: Sign up to [beta testing in the discussion forum]!
* :green_heart: Become a [GitHub sponsor] to help fund my open-source projects!

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
[GitHub sponsor]: https://github.com/sponsors/marciot
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

On a color Mac, the encoding process works like this:

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

#### Performance and Assembly Language Tricks

The TRLE encoder was written in 68x assembly for best performance.
For the color encoders, a 68020 is assumed and the code makes special
use of 68020 instructions such as `bfextu`. Most functions take
advantage of as many of the available eight data and eight address
registers as possible, as to minimize memory accesses.

</details>

[Fletcher's checksum]: https://en.wikipedia.org/wiki/Fletcher%27s_checksum
[beta testing in the discussion forum]: https://github.com/marciot/mac-minivnc/discussions/1