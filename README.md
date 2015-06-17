# Stamp

Stamp is a command line note-taking software for POSIX compatible systems. At the moment it's tested on GNU/Linux and on FreeBSD, but it should be very portable for other platforms. It is forked from [Memo] by Niko Rosvall at version 1.4. The main difference being that Stamp supports categories and Memo focusses on status of notes, like todo's. Stamp is inspired by [Stampnote] for OSX.

## Example usage
```sh
$ stamp -a movies "Cloudy with a chance of meatballs"
$ stamp -s movies
1   2014-11-15  Frozen
2   2014-12-01  Cloudy with a chance of meatballs.
$ stamp -d movies 1
```
[Memo]:http://getmemo.org
[Stampnote]:http://slidetorock.com
