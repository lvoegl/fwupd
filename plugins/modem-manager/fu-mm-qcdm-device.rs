
// Copyright 2025 Lukas Voegl <lukas@voegl.org>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[repr(u8)]
enum FuMmQcdmDeviceSpecialChar {
    Control = 0x7E,
    Escape = 0x7D,
}

#[repr(u8)]
enum FuMmQcdmDeviceCommand {
    Subsystem = 0x4B,
}

#[repr(u8)]
enum FuMmQcdmDeviceSubsystem {
    Operations = 0x65,
}

#[repr(u16)]
enum FuMmQcdmDeviceSubsystemCommand {
    RebootEdl = 0x01,
}


#[derive(Default, New, Parse)]
struct FuMmQcdmDevicePacketData {
    command: FuMmQcdmDeviceCommand,
    subsystem: FuMmQcdmDeviceSubsystem,
    subsystem_command: FuMmQcdmDeviceSubsystemCommand,
}

#[derive(Default, New, Parse)]
struct FuMmQcdmDeviceBasePacket {
    data: FuMmQcdmDevicePacketData,
    crc: u16le,
}
