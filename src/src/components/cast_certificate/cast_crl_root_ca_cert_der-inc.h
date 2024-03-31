// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Certificate:
//     Data:
//         Version: 3 (0x2)
//         Serial Number: 153 (0x99)
//     Signature Algorithm: sha256WithRSAEncryption
//         Issuer: C=US, ST=California, L=Mountain View, O=Google Inc,
//                 OU=Cast, CN=Cast CRL Root CA
//         Validity
//             Not Before: Aug  1 21:47:47 2016 GMT
//             Not After : Jul 27 21:47:47 2036 GMT
//         Subject: C=US, ST=California, L=Mountain View, O=Google Inc, OU=Cast,
//                  CN=Cast CRL Root CA
//         Subject Public Key Info:
//             Public Key Algorithm: rsaEncryption
//                 Public-Key: (2048 bit)
//                 Modulus:
//                     00:c2:7f:c0:09:21:d3:60:89:28:b5:96:6e:fe:a6:
//                     ad:fe:ae:e0:66:35:bd:99:6e:e8:93:85:29:ba:de:
//                     44:5d:a8:6b:fc:e6:cc:37:dd:1d:0f:cf:1e:3a:32:
//                     2c:7f:e0:1b:c9:bb:4c:34:a9:1c:97:b5:f8:6d:42:
//                     9c:4d:06:6a:a0:2d:95:55:3f:78:1d:5c:ab:e9:3a:
//                     a6:08:3b:5a:af:f4:ab:53:77:14:9a:6b:b2:37:2e:
//                     cd:6e:ea:bc:22:5d:56:55:73:fd:bd:03:2f:54:5e:
//                     7f:8b:c1:74:36:1a:18:1f:64:de:bf:08:80:4a:12:
//                     0c:49:53:b8:c7:3b:db:5f:dc:59:77:2f:b8:3a:05:
//                     8a:f6:b7:47:2a:9b:74:63:08:31:12:e6:7b:44:d1:
//                     c1:7c:c8:87:b8:50:63:6d:9f:d7:ba:36:53:72:47:
//                     5f:dc:43:43:eb:d7:2e:11:d1:8a:7a:a4:03:f2:6a:
//                     d3:88:e6:a7:b8:9d:81:b2:b0:88:24:c8:a1:fa:b0:
//                     aa:db:08:64:3e:8b:2a:07:5c:5a:82:05:99:c2:d5:
//                     ca:52:75:21:a7:fa:c5:a1:da:ac:f7:fe:d0:c7:44:
//                     76:9a:eb:6b:d3:bd:f4:7a:31:a6:ad:2f:5a:c4:31:
//                     3a:6d:f1:dd:7b:44:81:37:cf:13:85:5d:96:ae:7b:
//                     96:2b
//                 Exponent: 65537 (0x10001)
//         X509v3 extensions:
//             X509v3 Basic Constraints:
//                 CA:TRUE, pathlen:1
//             X509v3 Subject Key Identifier:
//                 1A:65:12:B4:A9:B9:B4:FC:91:0C:9E:67:E0:5B:D9:C9:AD:44:1C:B9
//             X509v3 Authority Key Identifier:
//                 keyid:1A:65:12:B4:A9:B9:B4:FC:91:0C:9E:67:E0:5B:D9:C9:AD:44
//                      :1C:B9
//
//             X509v3 Key Usage:
//                 Certificate Sign
//     Signature Algorithm: sha256WithRSAEncryption
//          af:5f:8b:c0:f7:c5:26:88:b9:ac:f7:ec:4d:0f:76:ab:e2:74:
//          9a:44:3c:33:f6:74:3d:04:2a:59:76:a2:05:27:c4:e3:a2:c8:
//          c2:af:7e:fd:be:b9:ca:e9:5b:a8:2a:cd:a7:1e:0e:37:f1:6f:
//          84:5e:aa:42:1f:ba:f0:44:ba:db:87:61:68:91:bb:1d:5c:3a:
//          f0:8e:02:20:76:aa:47:99:c7:73:0d:90:32:4a:b9:e3:fd:11:
//          8b:5d:bd:22:4d:05:75:17:61:a2:a6:4f:b0:3d:52:8e:aa:c9:
//          b4:8d:05:5a:1c:36:c1:7b:87:f7:f8:e4:81:36:27:ec:35:ae:
//          b9:ce:15:47:e1:10:c9:16:69:3a:22:8e:63:18:31:cc:3b:56:
//          69:c6:d4:24:dd:95:25:cf:34:e6:00:ae:e1:87:1e:ee:0c:14:
//          dc:0d:82:81:31:1f:8f:6d:d2:c0:e1:7c:12:f7:9d:ca:02:e3:
//          76:36:44:53:3a:87:71:7d:ed:32:4c:a4:96:e6:e5:2c:c7:0d:
//          b7:96:c0:f3:7d:e5:58:32:f7:25:25:c0:13:76:d0:76:6c:73:
//          ab:3d:15:cd:c5:e8:85:15:9a:02:52:e9:61:41:e2:66:01:c5:
//          71:e5:db:c0:a5:b3:4c:1e:ac:93:8a:35:4c:4d:da:57:22:24:
//          1d:3a:f6:bd
const unsigned char kCastCRLRootCaDer[] = {
    0x30, 0x82, 0x03, 0xce, 0x30, 0x82, 0x02, 0xb6, 0xa0, 0x03, 0x02, 0x01,
    0x02, 0x02, 0x02, 0x00, 0x99, 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48,
    0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0b, 0x05, 0x00, 0x30, 0x79, 0x31, 0x0b,
    0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06, 0x13, 0x02, 0x55, 0x53, 0x31,
    0x13, 0x30, 0x11, 0x06, 0x03, 0x55, 0x04, 0x08, 0x0c, 0x0a, 0x43, 0x61,
    0x6c, 0x69, 0x66, 0x6f, 0x72, 0x6e, 0x69, 0x61, 0x31, 0x16, 0x30, 0x14,
    0x06, 0x03, 0x55, 0x04, 0x07, 0x0c, 0x0d, 0x4d, 0x6f, 0x75, 0x6e, 0x74,
    0x61, 0x69, 0x6e, 0x20, 0x56, 0x69, 0x65, 0x77, 0x31, 0x13, 0x30, 0x11,
    0x06, 0x03, 0x55, 0x04, 0x0a, 0x0c, 0x0a, 0x47, 0x6f, 0x6f, 0x67, 0x6c,
    0x65, 0x20, 0x49, 0x6e, 0x63, 0x31, 0x0d, 0x30, 0x0b, 0x06, 0x03, 0x55,
    0x04, 0x0b, 0x0c, 0x04, 0x43, 0x61, 0x73, 0x74, 0x31, 0x19, 0x30, 0x17,
    0x06, 0x03, 0x55, 0x04, 0x03, 0x0c, 0x10, 0x43, 0x61, 0x73, 0x74, 0x20,
    0x43, 0x52, 0x4c, 0x20, 0x52, 0x6f, 0x6f, 0x74, 0x20, 0x43, 0x41, 0x30,
    0x1e, 0x17, 0x0d, 0x31, 0x36, 0x30, 0x38, 0x30, 0x31, 0x32, 0x31, 0x34,
    0x37, 0x34, 0x37, 0x5a, 0x17, 0x0d, 0x33, 0x36, 0x30, 0x37, 0x32, 0x37,
    0x32, 0x31, 0x34, 0x37, 0x34, 0x37, 0x5a, 0x30, 0x79, 0x31, 0x0b, 0x30,
    0x09, 0x06, 0x03, 0x55, 0x04, 0x06, 0x13, 0x02, 0x55, 0x53, 0x31, 0x13,
    0x30, 0x11, 0x06, 0x03, 0x55, 0x04, 0x08, 0x0c, 0x0a, 0x43, 0x61, 0x6c,
    0x69, 0x66, 0x6f, 0x72, 0x6e, 0x69, 0x61, 0x31, 0x16, 0x30, 0x14, 0x06,
    0x03, 0x55, 0x04, 0x07, 0x0c, 0x0d, 0x4d, 0x6f, 0x75, 0x6e, 0x74, 0x61,
    0x69, 0x6e, 0x20, 0x56, 0x69, 0x65, 0x77, 0x31, 0x13, 0x30, 0x11, 0x06,
    0x03, 0x55, 0x04, 0x0a, 0x0c, 0x0a, 0x47, 0x6f, 0x6f, 0x67, 0x6c, 0x65,
    0x20, 0x49, 0x6e, 0x63, 0x31, 0x0d, 0x30, 0x0b, 0x06, 0x03, 0x55, 0x04,
    0x0b, 0x0c, 0x04, 0x43, 0x61, 0x73, 0x74, 0x31, 0x19, 0x30, 0x17, 0x06,
    0x03, 0x55, 0x04, 0x03, 0x0c, 0x10, 0x43, 0x61, 0x73, 0x74, 0x20, 0x43,
    0x52, 0x4c, 0x20, 0x52, 0x6f, 0x6f, 0x74, 0x20, 0x43, 0x41, 0x30, 0x82,
    0x01, 0x22, 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d,
    0x01, 0x01, 0x01, 0x05, 0x00, 0x03, 0x82, 0x01, 0x0f, 0x00, 0x30, 0x82,
    0x01, 0x0a, 0x02, 0x82, 0x01, 0x01, 0x00, 0xc2, 0x7f, 0xc0, 0x09, 0x21,
    0xd3, 0x60, 0x89, 0x28, 0xb5, 0x96, 0x6e, 0xfe, 0xa6, 0xad, 0xfe, 0xae,
    0xe0, 0x66, 0x35, 0xbd, 0x99, 0x6e, 0xe8, 0x93, 0x85, 0x29, 0xba, 0xde,
    0x44, 0x5d, 0xa8, 0x6b, 0xfc, 0xe6, 0xcc, 0x37, 0xdd, 0x1d, 0x0f, 0xcf,
    0x1e, 0x3a, 0x32, 0x2c, 0x7f, 0xe0, 0x1b, 0xc9, 0xbb, 0x4c, 0x34, 0xa9,
    0x1c, 0x97, 0xb5, 0xf8, 0x6d, 0x42, 0x9c, 0x4d, 0x06, 0x6a, 0xa0, 0x2d,
    0x95, 0x55, 0x3f, 0x78, 0x1d, 0x5c, 0xab, 0xe9, 0x3a, 0xa6, 0x08, 0x3b,
    0x5a, 0xaf, 0xf4, 0xab, 0x53, 0x77, 0x14, 0x9a, 0x6b, 0xb2, 0x37, 0x2e,
    0xcd, 0x6e, 0xea, 0xbc, 0x22, 0x5d, 0x56, 0x55, 0x73, 0xfd, 0xbd, 0x03,
    0x2f, 0x54, 0x5e, 0x7f, 0x8b, 0xc1, 0x74, 0x36, 0x1a, 0x18, 0x1f, 0x64,
    0xde, 0xbf, 0x08, 0x80, 0x4a, 0x12, 0x0c, 0x49, 0x53, 0xb8, 0xc7, 0x3b,
    0xdb, 0x5f, 0xdc, 0x59, 0x77, 0x2f, 0xb8, 0x3a, 0x05, 0x8a, 0xf6, 0xb7,
    0x47, 0x2a, 0x9b, 0x74, 0x63, 0x08, 0x31, 0x12, 0xe6, 0x7b, 0x44, 0xd1,
    0xc1, 0x7c, 0xc8, 0x87, 0xb8, 0x50, 0x63, 0x6d, 0x9f, 0xd7, 0xba, 0x36,
    0x53, 0x72, 0x47, 0x5f, 0xdc, 0x43, 0x43, 0xeb, 0xd7, 0x2e, 0x11, 0xd1,
    0x8a, 0x7a, 0xa4, 0x03, 0xf2, 0x6a, 0xd3, 0x88, 0xe6, 0xa7, 0xb8, 0x9d,
    0x81, 0xb2, 0xb0, 0x88, 0x24, 0xc8, 0xa1, 0xfa, 0xb0, 0xaa, 0xdb, 0x08,
    0x64, 0x3e, 0x8b, 0x2a, 0x07, 0x5c, 0x5a, 0x82, 0x05, 0x99, 0xc2, 0xd5,
    0xca, 0x52, 0x75, 0x21, 0xa7, 0xfa, 0xc5, 0xa1, 0xda, 0xac, 0xf7, 0xfe,
    0xd0, 0xc7, 0x44, 0x76, 0x9a, 0xeb, 0x6b, 0xd3, 0xbd, 0xf4, 0x7a, 0x31,
    0xa6, 0xad, 0x2f, 0x5a, 0xc4, 0x31, 0x3a, 0x6d, 0xf1, 0xdd, 0x7b, 0x44,
    0x81, 0x37, 0xcf, 0x13, 0x85, 0x5d, 0x96, 0xae, 0x7b, 0x96, 0x2b, 0x02,
    0x03, 0x01, 0x00, 0x01, 0xa3, 0x60, 0x30, 0x5e, 0x30, 0x0f, 0x06, 0x03,
    0x55, 0x1d, 0x13, 0x04, 0x08, 0x30, 0x06, 0x01, 0x01, 0xff, 0x02, 0x01,
    0x01, 0x30, 0x1d, 0x06, 0x03, 0x55, 0x1d, 0x0e, 0x04, 0x16, 0x04, 0x14,
    0x1a, 0x65, 0x12, 0xb4, 0xa9, 0xb9, 0xb4, 0xfc, 0x91, 0x0c, 0x9e, 0x67,
    0xe0, 0x5b, 0xd9, 0xc9, 0xad, 0x44, 0x1c, 0xb9, 0x30, 0x1f, 0x06, 0x03,
    0x55, 0x1d, 0x23, 0x04, 0x18, 0x30, 0x16, 0x80, 0x14, 0x1a, 0x65, 0x12,
    0xb4, 0xa9, 0xb9, 0xb4, 0xfc, 0x91, 0x0c, 0x9e, 0x67, 0xe0, 0x5b, 0xd9,
    0xc9, 0xad, 0x44, 0x1c, 0xb9, 0x30, 0x0b, 0x06, 0x03, 0x55, 0x1d, 0x0f,
    0x04, 0x04, 0x03, 0x02, 0x02, 0x04, 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86,
    0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0b, 0x05, 0x00, 0x03, 0x82, 0x01,
    0x01, 0x00, 0xaf, 0x5f, 0x8b, 0xc0, 0xf7, 0xc5, 0x26, 0x88, 0xb9, 0xac,
    0xf7, 0xec, 0x4d, 0x0f, 0x76, 0xab, 0xe2, 0x74, 0x9a, 0x44, 0x3c, 0x33,
    0xf6, 0x74, 0x3d, 0x04, 0x2a, 0x59, 0x76, 0xa2, 0x05, 0x27, 0xc4, 0xe3,
    0xa2, 0xc8, 0xc2, 0xaf, 0x7e, 0xfd, 0xbe, 0xb9, 0xca, 0xe9, 0x5b, 0xa8,
    0x2a, 0xcd, 0xa7, 0x1e, 0x0e, 0x37, 0xf1, 0x6f, 0x84, 0x5e, 0xaa, 0x42,
    0x1f, 0xba, 0xf0, 0x44, 0xba, 0xdb, 0x87, 0x61, 0x68, 0x91, 0xbb, 0x1d,
    0x5c, 0x3a, 0xf0, 0x8e, 0x02, 0x20, 0x76, 0xaa, 0x47, 0x99, 0xc7, 0x73,
    0x0d, 0x90, 0x32, 0x4a, 0xb9, 0xe3, 0xfd, 0x11, 0x8b, 0x5d, 0xbd, 0x22,
    0x4d, 0x05, 0x75, 0x17, 0x61, 0xa2, 0xa6, 0x4f, 0xb0, 0x3d, 0x52, 0x8e,
    0xaa, 0xc9, 0xb4, 0x8d, 0x05, 0x5a, 0x1c, 0x36, 0xc1, 0x7b, 0x87, 0xf7,
    0xf8, 0xe4, 0x81, 0x36, 0x27, 0xec, 0x35, 0xae, 0xb9, 0xce, 0x15, 0x47,
    0xe1, 0x10, 0xc9, 0x16, 0x69, 0x3a, 0x22, 0x8e, 0x63, 0x18, 0x31, 0xcc,
    0x3b, 0x56, 0x69, 0xc6, 0xd4, 0x24, 0xdd, 0x95, 0x25, 0xcf, 0x34, 0xe6,
    0x00, 0xae, 0xe1, 0x87, 0x1e, 0xee, 0x0c, 0x14, 0xdc, 0x0d, 0x82, 0x81,
    0x31, 0x1f, 0x8f, 0x6d, 0xd2, 0xc0, 0xe1, 0x7c, 0x12, 0xf7, 0x9d, 0xca,
    0x02, 0xe3, 0x76, 0x36, 0x44, 0x53, 0x3a, 0x87, 0x71, 0x7d, 0xed, 0x32,
    0x4c, 0xa4, 0x96, 0xe6, 0xe5, 0x2c, 0xc7, 0x0d, 0xb7, 0x96, 0xc0, 0xf3,
    0x7d, 0xe5, 0x58, 0x32, 0xf7, 0x25, 0x25, 0xc0, 0x13, 0x76, 0xd0, 0x76,
    0x6c, 0x73, 0xab, 0x3d, 0x15, 0xcd, 0xc5, 0xe8, 0x85, 0x15, 0x9a, 0x02,
    0x52, 0xe9, 0x61, 0x41, 0xe2, 0x66, 0x01, 0xc5, 0x71, 0xe5, 0xdb, 0xc0,
    0xa5, 0xb3, 0x4c, 0x1e, 0xac, 0x93, 0x8a, 0x35, 0x4c, 0x4d, 0xda, 0x57,
    0x22, 0x24, 0x1d, 0x3a, 0xf6, 0xbd,
};
