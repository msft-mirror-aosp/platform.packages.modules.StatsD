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

package android.app;

import android.annotation.NonNull;
import android.annotation.SystemApi;
import android.annotation.SuppressLint;
import android.database.AbstractCursor;
import android.database.MatrixCursor;

/**
 * Custom cursor implementation to hold a cross-process cursor to pass data to caller.
 *
 * @hide
 */
@SystemApi
public class StatsCursor extends AbstractCursor {
    private final MatrixCursor mMatrixCursor;
    private final int[] mColumnTypes;
    private final String[] mColumnNames;
    private final int mRowCount;

    /**
     * @hide
     **/
    public StatsCursor(String[] queryData, String[] columnNames, int[] columnTypes, int rowCount) {
        mColumnTypes = columnTypes;
        mColumnNames = columnNames;
        mRowCount = rowCount;
        mMatrixCursor = new MatrixCursor(columnNames);
        for (int i = 0; i < rowCount; i++) {
            MatrixCursor.RowBuilder builder = mMatrixCursor.newRow();
            for (int j = 0; j < columnNames.length; j++) {
                int dataIndex = i * columnNames.length + j;
                builder.add(columnNames[j], queryData[dataIndex]);
            }
        }
    }

    /**
     * {@inheritDoc}
     */
    @Override
    public int getCount() {
        return mRowCount;
    }

    /**
     * {@inheritDoc}
     */
    @Override
    @NonNull
    public String[] getColumnNames() {
        return mColumnNames;
    }

    /**
     * {@inheritDoc}
     */
    @Override
    @NonNull
    public String getString(int column) {
        return mMatrixCursor.getString(column);
    }

    /**
     * {@inheritDoc}
     */
    @Override
    @SuppressLint("NoByteOrShort")
    public short getShort(int column) {
        return mMatrixCursor.getShort(column);
    }

    /**
     * {@inheritDoc}
     */
    @Override
    public int getInt(int column) {
        return mMatrixCursor.getInt(column);
    }

    /**
     * {@inheritDoc}
     */
    @Override
    public long getLong(int column) {
        return mMatrixCursor.getLong(column);
    }

    /**
     * {@inheritDoc}
     */
    @Override
    public float getFloat(int column) {
        return mMatrixCursor.getFloat(column);
    }

    /**
     * {@inheritDoc}
     */
    @Override
    public double getDouble(int column) {
        return mMatrixCursor.getDouble(column);
    }

    /**
     * {@inheritDoc}
     */
    @Override
    public boolean isNull(int column) {
        return mMatrixCursor.isNull(column);
    }

    /**
     * {@inheritDoc}
     */
    @Override
    public int getType(int column) {
        return mColumnTypes[column];
    }

    /**
     * {@inheritDoc}
     */
    @Override
    public boolean onMove(int oldPosition, int newPosition) {
        return mMatrixCursor.moveToPosition(newPosition);
    }
}
