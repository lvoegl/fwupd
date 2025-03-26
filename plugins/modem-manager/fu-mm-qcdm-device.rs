// Copyright 2025 Lukas Voegl <lukas@voegl.org>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[repr(u8)]
enum FuMmQcdmDeviceSpecialChar {
    Control = 0x7E,
    Escape = 0x7D,
}

#[repr(u8)]
enum FuMmQcdmDeviceCommand {
    Command = 0x4B,
}

#[repr(u8)]
enum FuMmQcdmDeviceSubsystem {
    Operations = 0x65,
}

#[repr(u16le)]
enum FuMmQcdmDeviceSubsystemCommand {
    RebootEdl = 0x01,
}

#[derive(Default, New, Parse)]
struct FuMmQcdmDeviceSubsystemReq {
    command: FuMmQcdmDeviceCommand,
    subsystem: FuMmQcdmDeviceSubsystem,
    subsystem_command: FuMmQcdmDeviceSubsystemCommand,
}

#[derive(Default, New, Parse)]
struct FuMmQcdmDevicePktRebootEdlReqRes {
    payload: FuMmQcdmDeviceSubsystemReq,
    crc: u16le,
}
