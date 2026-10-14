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

## 1. Record

1. Start Wireshark, pick the USBPcap interface whose bus lists the Seiren (USBPcap shows the
   devices on each bus; look for the Razer Seiren V3 Chroma, USB id `1532:056f`), and start a
   capture.
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

On any machine with `tshark` (Windows, or Linux with the capture copied over):

```
tshark -r seiren.pcapng -x > seiren.txt
peri decode --scan seiren.txt --summary
```

`--summary` prints each distinct command once, with how many times it was seen and a sample, and
says at the end how many commands periferi does not know yet. Without `--summary` it prints every
frame with its packet number, so a frame can be matched to your notes: to get each packet's time,

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
- It was tested on a dump laid out the way `tshark -x` prints (the real Wireshark text layout),
  built from frames captured from the real microphone, but not on a real Synapse capture.
- Some Synapse settings may only live in software (Stream Mixer, presets, Chroma Studio effects)
  and never reach the microphone, so they will not appear.
