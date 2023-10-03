/*
 * Copyright (C) 2023 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package com.android.statsd.shelltools;

import com.android.internal.os.ExperimentIdsProto;
import com.android.internal.os.UidDataProto;
import com.android.os.ActiveConfigProto;
import com.android.os.ShellConfig;
import com.android.os.art.ArtExtensionAtoms;
import com.android.os.art.BackgroundExtensionDexoptAtoms;
import com.android.os.art.OdrefreshExtensionAtoms;
import com.android.os.bluetooth.BluetoothExtensionAtoms;
import com.android.os.corenetworking.connectivity.ConnectivityAtoms;
import com.android.os.corenetworking.connectivity.ConnectivityExtensionAtoms;
import com.android.os.expresslog.ExpresslogExtensionAtoms;
import com.android.os.framework.FrameworkExtensionAtoms;
import com.android.os.gps.GpsAtoms;
import com.android.os.media.MediaDrmAtoms;
import com.android.os.statsd.ShellDataProto;
import android.os.statsd.media.MediaCodecExtensionAtoms;
import com.android.os.rkpd.RkpdExtensionAtoms;
import com.android.os.sdksandbox.SdksandboxExtensionAtoms;
import com.android.os.threadnetwork.ThreadnetworkExtensionAtoms;

import com.google.protobuf.ExtensionRegistry;

/**
 * CustomExtensionRegistry for local use of statsd.
 */
public class CustomExtensionRegistry {

    public static ExtensionRegistry REGISTRY;

    static {
        /** In Java, when parsing a message containing extensions, you must provide an
         * ExtensionRegistry which contains definitions of all of the extensions which you
         * want the parser to recognize. This is necessary because Java's bytecode loading
         * semantics do not provide any way for the protocol buffers library to automatically
         * discover all extensions defined in your binary.
         *
         * See http://sites/protocol-buffers/user-docs/miscellaneous-howtos/extensions
         * #Java_ExtensionRegistry_
         */
        REGISTRY = ExtensionRegistry.newInstance();
        registerAllExtensions(REGISTRY);
        REGISTRY = REGISTRY.getUnmodifiable();
    }

    /**
     * Registers all proto2 extensions.
     */
    private static void registerAllExtensions(ExtensionRegistry extensionRegistry) {
        ExperimentIdsProto.registerAllExtensions(extensionRegistry);
        UidDataProto.registerAllExtensions(extensionRegistry);
        ActiveConfigProto.registerAllExtensions(extensionRegistry);
        ShellConfig.registerAllExtensions(extensionRegistry);
        ArtExtensionAtoms.registerAllExtensions(extensionRegistry);
        BackgroundExtensionDexoptAtoms.registerAllExtensions(extensionRegistry);
        OdrefreshExtensionAtoms.registerAllExtensions(extensionRegistry);
        BluetoothExtensionAtoms.registerAllExtensions(extensionRegistry);
        ConnectivityAtoms.registerAllExtensions(extensionRegistry);
        ConnectivityExtensionAtoms.registerAllExtensions(extensionRegistry);
        ExpresslogExtensionAtoms.registerAllExtensions(extensionRegistry);
        FrameworkExtensionAtoms.registerAllExtensions(extensionRegistry);
        GpsAtoms.registerAllExtensions(extensionRegistry);
        MediaDrmAtoms.registerAllExtensions(extensionRegistry);
        ShellDataProto.registerAllExtensions(extensionRegistry);
        MediaCodecExtensionAtoms.registerAllExtensions(extensionRegistry);
        SdksandboxExtensionAtoms.registerAllExtensions(extensionRegistry);
        RkpdExtensionAtoms.registerAllExtensions(extensionRegistry);
        ThreadnetworkExtensionAtoms.registerAllExtensions(extensionRegistry);
    }
}
