/*
 * Copyright (C) 2019 The Android Open Source Project
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
package android.cts.statsd.subscriber;

import static com.google.common.truth.Truth.assertThat;
import static com.google.common.truth.Truth.assertWithMessage;

import com.android.compatibility.common.util.CpuFeatures;
import com.android.internal.os.StatsdConfigProto;
import com.android.os.AtomsProto.Atom;
import com.android.os.AtomsProto.SystemUptime;
import com.android.os.ShellConfig;
import com.android.os.statsd.ShellDataProto;
import com.android.tradefed.device.CollectingByteOutputReceiver;
import com.android.tradefed.device.DeviceNotAvailableException;
import com.android.tradefed.device.ITestDevice;
import com.android.tradefed.log.LogUtil;
import com.android.tradefed.testtype.DeviceTestCase;
import com.google.common.io.Files;
import com.google.protobuf.InvalidProtocolBufferException;

import java.io.File;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.Arrays;
import java.util.ArrayList;
import java.util.concurrent.TimeUnit;
import android.cts.statsd.atom.AtomTestCase;

/**
 * Statsd shell data subscription test.
 */
public class ShellSubscriberTest extends AtomTestCase {
    private int sizetBytes;

    // ArrayList to keep track of all active spawned data-subscribe processes
    ArrayList<Process> processList = new ArrayList<Process>();

    @Override
    protected void setUp() throws Exception {
        super.setUp();
        sizetBytes = getSizetBytes();
    }

    private void killProcess(Process process) throws Exception {
        // Sending SIGINT to end subscription and then waiting to make sure
        // process terminated. Note: some resources may need more time to
        // be deconstructed; e.g. private thread
        Runtime runtime = Runtime.getRuntime();
        runtime.exec("kill -SIGINT " + String.valueOf(process.pid()));
        process.waitFor();
    }

    @Override
    protected void tearDown() throws Exception {
        // Kill all remaining processes if any
        for (Process process : processList) {
            killProcess(process);
        }
        processList.clear();
        super.tearDown();
    }

    public void testShellSubscription() {
        if (sizetBytes < 0) {
            return;
        }

        CollectingByteOutputReceiver receiver = startSubscription();
        checkOutput(receiver);
    }

    // This is testShellSubscription but 5x
    public void testShellSubscriptionReconnect() {
        int numOfSubs = 5;
        if (sizetBytes < 0) {
            return;
        }

        for (int i = 0; i < numOfSubs; i++) {
            CollectingByteOutputReceiver receiver = startSubscription();
            checkOutput(receiver);
        }
    }

    // Tests that multiple clients can run at once and ignores subscription requests
    // after the subscription limit is hit (20 active subscriptions).
    public void testShellMaxSubscriptions() {
        // Maximum number of active subscriptions, set in ShellSubscriber.h
        int maxSubs = 20;
        if (sizetBytes < 0) {
            return;
        }
        CollectingByteOutputReceiver[] receivers = new CollectingByteOutputReceiver[maxSubs + 1];
        Process[] processes = new Process[maxSubs + 1];
        ShellConfig.ShellSubscription config = createConfig();
        byte[] validConfig = makeValidConfig(config);
        // timeout of -1 means the subscription won't timeout
        int timeout = -1;
        try {
            for (int i = 0; i < maxSubs; i++) {
                processes[i] = runDataSubscribe(validConfig, timeout);
            }
            // Sleep 2.5 seconds to make sure all subscription clients are initialized before
            // first pushed event
            Thread.sleep(2500);

            // arbitrary label = 1
            doAppBreadcrumbReported(1);

            // Sleep 2.5 seconds to make sure the processes read the breadcrumb before being killed.
            Thread.sleep(2500);

            for (int i = 1; i < maxSubs; i++) {
                killProcess(processes[i]);
                processList.remove(processes[i]);
                receivers[i] = readData(processes[i]);
                checkOutput(receivers[i]);
            }
            // Sleep 2.5 seconds to make sure the processes are killed and resources are released.
            Thread.sleep(2500);

            for (int i = 1; i < maxSubs; i++) {
                processes[i] = runDataSubscribe(validConfig, timeout);
            }
            // Sleep 2.5 seconds to make sure all subscription clients are initialized before
            // pushed event
            Thread.sleep(2500);

            // arbitrary label = 1
            doAppBreadcrumbReported(1);

            // Sleep 2.5 seconds to make sure the processes read the breadcrumb before being killed.
            Thread.sleep(2500);

            // ShellSubscriber only allows 20 subscriptions at a time. This is the 21st which will
            // be ignored
            processes[maxSubs] = runDataSubscribe(validConfig, timeout);

            // Sleep 2.5 seconds to make sure the invalid subscription attempts to run.
            Thread.sleep(2500);

            for (int i = 0; i <= maxSubs; i++) {
                killProcess(processes[i]);
                processList.remove(processes[i]);
                receivers[i] = readData(processes[i]);
            }
            // Sleep 2.5 seconds to make sure the processes are killed and resources are released.
            Thread.sleep(2500);
        } catch (Exception e) {
            fail(e.getMessage());
        }
        for (int i = 0; i < maxSubs; i++) {
            checkOutput(receivers[i]);
        }
        byte[] output = receivers[maxSubs].getOutput();
        assertThat(output.length).isEqualTo(0);
    }

    private int getSizetBytes() {
        try {
            ITestDevice device = getDevice();
            if (CpuFeatures.isArm64(device)) {
                return 8;
            }
            if (CpuFeatures.isArm32(device)) {
                return 4;
            }
            return -1;
        } catch (DeviceNotAvailableException e) {
            return -1;
        }
    }

    // Choose a pulled atom that is likely to be supported on all devices (SYSTEM_UPTIME). Testing
    // pushed atoms is trickier because executeShellCommand() is blocking, so we cannot push a
    // breadcrumb event while the shell subscription is running.
    private ShellConfig.ShellSubscription createConfig() {
        return ShellConfig.ShellSubscription.newBuilder()
                .addPushed((StatsdConfigProto.SimpleAtomMatcher.newBuilder()
                                .setAtomId(Atom.APP_BREADCRUMB_REPORTED_FIELD_NUMBER))
                        .build()).build();
    }

    private byte[] makeValidConfig(ShellConfig.ShellSubscription config) {
        int length = config.toByteArray().length;
        byte[] validConfig = new byte[sizetBytes + length];
        System.arraycopy(IntToByteArrayLittleEndian(length), 0, validConfig, 0, sizetBytes);
        System.arraycopy(config.toByteArray(), 0, validConfig, sizetBytes, length);
        return validConfig;
    }

    private Process runDataSubscribe(byte[] validConfig, int timeout) throws Exception {
        Runtime runtime = Runtime.getRuntime();
        Process process = runtime.exec("adb shell cmd stats data-subscribe " + timeout);
        LogUtil.CLog.d("Starting new shell subscription.");
        processList.add(process);
        OutputStream stdin = process.getOutputStream();
        stdin.write(validConfig);
        stdin.close();
        return process;
    }

    private CollectingByteOutputReceiver readData(Process process) throws Exception {
        // Reading shell_data and passing it to the receiver struct
        InputStream stdout = process.getInputStream();
        byte[] output = stdout.readAllBytes();
        LogUtil.CLog.d("output.length in readData: " + output.length);
        CollectingByteOutputReceiver receiver = new CollectingByteOutputReceiver();
        receiver.addOutput(output, 0, output.length);
        stdout.close();
        return receiver;
    }

    private CollectingByteOutputReceiver startSubscription() {
        ShellConfig.ShellSubscription config = createConfig();
        LogUtil.CLog.d("Uploading the following config:\n" + config.toString());
        byte[] validConfig = makeValidConfig(config);
        int timeout = 2;
        try {
            Process process = runDataSubscribe(validConfig, timeout);
            // Sleep a second to make sure subscription is initiated
            Thread.sleep(1000);
            // arbitrary label = 1
            doAppBreadcrumbReported(1);
            // Wait for process to timeout. If the process does not timeout, kill the process
            if (!process.waitFor(2, TimeUnit.SECONDS)) {
                killProcess(process);
            }
            processList.remove(process);
            return readData(process);
        } catch (Exception e) {
            fail(e.getMessage());
        }
        return new CollectingByteOutputReceiver();
    }

    private byte[] IntToByteArrayLittleEndian(int length) {
        ByteBuffer b = ByteBuffer.allocate(sizetBytes);
        b.order(ByteOrder.LITTLE_ENDIAN);
        b.putInt(length);
        return b.array();
    }

    // We do not know how much data will be returned, but we can check the data format.
    private void checkOutput(CollectingByteOutputReceiver receiver) {
        int atomCount = 0;
        int startIndex = 0;

        byte[] output = receiver.getOutput();
        LogUtil.CLog.d("output length in checkOutput: " + output.length);
        assertThat(output.length).isGreaterThan(0);
        while (output.length > startIndex) {
            assertThat(output.length).isAtLeast(startIndex + sizetBytes);
            int dataLength = readSizetFromByteArray(output, startIndex);
            if (dataLength == 0) {
                // We have received a heartbeat from statsd. This heartbeat isn't accompanied by any
                // atoms so return to top of while loop.
                startIndex += sizetBytes;
                continue;
            }
            assertThat(output.length).isAtLeast(startIndex + sizetBytes + dataLength);

            ShellDataProto.ShellData data = null;
            try {
                int dataStart = startIndex + sizetBytes;
                int dataEnd = dataStart + dataLength;
                data = ShellDataProto.ShellData.parseFrom(
                        Arrays.copyOfRange(output, dataStart, dataEnd));
            } catch (InvalidProtocolBufferException e) {
                fail("Failed to parse proto");
            }

            assertThat(data.getAtomCount()).isEqualTo(1);
            assertThat(data.getAtom(0).hasAppBreadcrumbReported()).isTrue();
            assertThat(data.getAtom(0).getAppBreadcrumbReported().getLabel()).isEqualTo(1);
            assertThat(data.getAtom(0).getAppBreadcrumbReported().getState().getNumber())
                       .isEqualTo(1);
            atomCount++;
            startIndex += sizetBytes + dataLength;
        }
        assertThat(atomCount).isGreaterThan(0);
    }

    // Converts the bytes in range [startIndex, startIndex + sizetBytes) from a little-endian array
    // into an integer. Even though sizetBytes could be greater than 4, we assume that the result
    // will fit within an int.
    private int readSizetFromByteArray(byte[] arr, int startIndex) {
        int value = 0;
        for (int j = 0; j < sizetBytes; j++) {
            value += ((int) arr[j + startIndex] & 0xffL) << (8 * j);
        }
        return value;
    }
}
