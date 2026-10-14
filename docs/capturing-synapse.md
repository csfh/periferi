# Capturing Razer Synapse talking to the Seiren

Nobody has published the commands behind most of the Seiren V3 Chroma's Synapse settings: the Mute
Effect colour, the Peaking and volume indicators, tap remapping, the audio effects (noise gate,
compressor, EQ and so on). The only way to learn them is to watch the software that does know them.
This is how to record that and turn it into a list, so the commands can be added to periferi.

It only records. Nothing here changes the microphone's firmware.

## What you need

- A Windows machine (or a Windows VM with the microphone passed through) with Razer Synapse
  installed and the Seiren connected.
- Wireshark with USBPcap, which the Wireshark installer offers. `tshark` comes with Wireshark.
- A Linux machine with periferi built, for `peri decode` (it is a Linux program; the capture and
  the text export can be made anywhere, and only the text file has to be copied over).

**First check that Synapse sees the microphone.** Early in this project a forum post said Synapse 4
does not support the plain Seiren V3 Chroma, and Razer's master guide is written for the 32-Bit DSP
model. If Synapse does not list your microphone and its settings, there is nothing to capture.

## 1. Record

1. Start Wireshark and pick the USBPcap interface for the bus the Seiren is on. In USBPcap's
   capture options, **tick only the Seiren** (USB id `1532:056f`) and untick every other device
   on that bus: a camera on the same bus (such as a webcam) would fill the capture with video
   data. Start the capture.
2. In Synapse, change **one setting at a time**, with a pause of about ten seconds between
   changes. Write each change down with the clock time, for example:

   ```
   12:00:10  Mute Effect: on
   12:00:25  Mute Effect colour: blue
   12:00:40  Peaking Indicator: on
   ```

   Worth doing, in this order: Mute Effect (toggle, then a colour), Peaking Indicator (toggle, then
   a colour), the microphone-volume and headphone-volume indicators, the brightness toggle,
   "switch off lighting when the display is off", each tap-count mapping if your Synapse offers
   one, and then the audio effects your model has (Noise Gate, Compressor, Limiter, High Pass
   Filter, Reverb, De-Esser, AI Noise Suppression, the equalizer). Change a value, then change it
   back, so every setting appears at least twice with different values.
3. Stop the capture and save it as `seiren.pcapng`.

## 2. Turn it into a list

Make the text export with `tshark` wherever it is installed, then copy `seiren.txt` to the Linux
machine and run `peri decode` there:

```
tshark -r seiren.pcapng -V -x > seiren.txt
peri decode --scan seiren.txt --summary
```

`-V` makes tshark print "Frame 123: ..." above every packet, which `peri decode` uses as the packet
number. Without it (plain `-x`) the numbers are only the order of the hex blocks in the file, which
usually match Wireshark's frame numbers but are not guaranteed to. The export can be narrowed to
the Seiren with a display filter such as `-Y "usb.device_address == 5"` (use the address USBPcap
showed for the microphone); check the field name in Wireshark's filter bar before relying on it.

`--summary` prints each distinct frame once, with how many times it was seen and a sample, and
says at the end how many commands periferi does not know yet and how many new variants of known
commands it saw. A "variant" is the same command with a different size or different first two
argument bytes (for the lighting commands, the storage byte and the LED id), which is where a
setting such as the Mute Effect is most likely to hide. Without `--summary` it prints every frame
with its packet number, so a frame can be matched to your notes. To get each packet's time:

```
tshark -r seiren.pcapng -T fields -e frame.number -e frame.time_relative
```

## 3. What to send back

The `--summary` output, the full listing (`peri decode --scan seiren.txt`), and your notes with the
times. A frame is a Razer command: a class and a command byte, a few argument bytes, and the
device's reply. Matching a command to a setting is done by lining the packet numbers up with your
notes: the frames between two of your changes belong to the second change.

## Capturing from Linux instead

If you run Synapse in a Windows VM on this machine, you can capture the host side of the USB
passthrough. This has not been tried here.

1. `sudo modprobe usbmon`, then find the bus with `lsusb`.
2. Pass the microphone through to the VM, for example with QEMU's
   `-device usb-host,vendorid=0x1532,productid=0x056f`. While the VM owns the device, Linux and the
   peri daemon cannot see it; stop the VM to get it back.
3. `sudo tshark -i usbmon<bus> -w seiren.pcapng`, then continue from "Turn it into a list". (The
   `peri decode` scanner looks at the packet bytes themselves, so it does not depend on how a
   capture format names its fields.)

## Limits

- The scanner finds frames of the Seiren's format: 64 bytes that start with report id `07` and
  whose checksum fits the request rule or the reply rule. A frame that does not fit either is not
  reported, so if a setting uses another report entirely, it will not show up. Tell me what the
  capture does contain (`tshark -r seiren.pcapng -z io,phs`) and the scanner can be widened.
- It was tested on dumps laid out the way `tshark -x` and `tshark -V -x` print, built from frames
  captured from the real microphone, but never on a real Synapse capture. If `peri decode` finds
  nothing in a real capture, the dump layout or the report format is different from what was
  assumed, and that is the first thing to look at.
- Some Synapse settings may only live in software (Stream Mixer, presets, Chroma Studio effects)
  and never reach the microphone, so they will not appear.
