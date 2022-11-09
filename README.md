# dsptunnel

Original dsptunnel by 50m30n3 (c) 2011

Forked version by Derkades (c) 2022 to use mono audio instead of stereo. This fork also better tolerates unstable connections.

## Introduction

Ever wanted to relive the carefree days of slow-as-fuck Internet?
Miss the days of aborted downloads and half loaded porn images?

Fret no more, your day has come!
Using dsptunnel you can now surf the Internet at the blazing fast speed of
nearly 3000 bytes per second. Thats almost 24 kilobits per second.
And its full duplex!

All you need are two computers with sound cards.
Connect the line-out of your first machine with the line-in of your second
machine and the line-out of your second machine with the line-in of the first.
Set your levels so you get a loud and clear signal on both ends.
Unity gain is best, so both playback and recording should be set to 0db.
Also, you'll need a stereo link, otherwise it won't work since this baby uses
differential signaling, if you know what I'm talking about.

## Usage

Now you need to create the TUNnel devices on both machines.
As root on both machines:
```
sudo ip tuntap add dev tun0 mode tun user $(id -u) group $(id -g)
sudo ip link set dev tun0 up
```

On machine 1:
```
sudo ip addr replace dev tun0 local 172.30.0.1 peer 172.30.0.2
```

On machine 2:
```
sudo ip addr replace dev tun0 local 172.30.0.2 peer 172.30.0.1
```

Feel free to replace the IPs with addresses of your choice (yay, FREEDOM).

Now simply run dsptunnel. If you don't have OSS run it using an OSS wrapper
like aoss or padsp. Now ping the other machine. Now set up routing and
enjoy your new slow Internet. And you can even listen to the bits.

## Missing /dev/dsp

Debian: sudo apt install osspd

## Configuration

If you have a really good sound card that supports 96khz sample rate you can
start dsptunnel with "-s 96000" to make use of it.
If you get trasmission errors you can increase the bit length using the -b
option. -t sets the name of the TUN device. -d sets the name of the dsp device.

## License

This software is licensed under the GNU GPL V3. See LICENSE for details.
