# com0com 3.0.0.0

Translation uses the unmodified com0com Windows driver to provide its upstream virtual serial port. The driver runs as a separate Windows component and is licensed under GPL version 2 or later. The license is reproduced in LICENSE.txt.

The two signed installers come from the [official signed distribution](https://sourceforge.net/projects/com0com/files/com0com/3.0.0.0/com0com-3.0.0.0-i386-and-x64-signed.zip/download). The archive SHA-256 is `6e5d4359865277430d4ae88c73fb7e648a0ed8e81aea5002478179cfcb0bb0e1`.

| Local file | Original name | SHA-256 |
| --- | --- | --- |
| installers/com0com-x86.exe | Setup_com0com_v3.0.0.0_W7_x86_signed.exe | de917425e022839f6374b509e3d34f3f9e9773915a44996194442337eadf4a5a |
| installers/com0com-x64.exe | Setup_com0com_v3.0.0.0_W7_x64_signed.exe | 26486b28604b49a9008c54feb11b9ece0008a8287ee5caf0bcf2a62f4317128f |

The corresponding original source distribution is included as com0com-3.0.0.0-source.zip. It is the unmodified [official source archive](https://sourceforge.net/projects/com0com/files/com0com/3.0.0.0/com0com-3.0.0.0.zip/download), SHA-256 `6751e911f73980b23cc878a456eb99d1dc6d0603c11c1d3ced109abe1c556380`.

Both installer signatures were verified as valid on the development machine. Windows decides whether the driver can load under the target machine's security policy. The application reports initialization failure when the driver or its ports are unavailable.

When distributing a running build that embeds these installers, also distribute this license, source archive and provenance file with it.
