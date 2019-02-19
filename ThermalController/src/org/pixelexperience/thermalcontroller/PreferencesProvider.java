/*
 * Copyright (c) 2019 The PixelExperience Project
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

package org.pixelexperience.thermalcontroller;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.UriMatcher;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.text.TextUtils;

import static com.android.internal.util.custom.thermal.ThermalController.*;

public class PreferencesProvider extends ContentProvider {
    private static final int PREFERENCES = 1;
    private static final UriMatcher uriMatcher;

    static {
        uriMatcher = new UriMatcher(UriMatcher.NO_MATCH);
        uriMatcher.addURI(AUTHORITY, "preferences/*", PREFERENCES);
    }

    @Override
    public boolean onCreate() {
        return false;
    }

    @Override
    public String getType(Uri uri) {
        return null;
    }

    @Override
    public Uri insert(Uri uri, ContentValues values) {
        if (uriMatcher.match(uri) != PREFERENCES) {
            throw new IllegalArgumentException("Unsupported URI: " + uri);
        }
        String packageName = uri.getPathSegments().get(1);
        if (TextUtils.isEmpty(packageName)){
            return null;
        }
        int profile = (int) values.get(COLUMN_PROFILE);
        Preferences.setProfileId(getContext(), packageName, profile);
        return Uri.parse(CONTENT_URI + "/" + packageName);
    }

    @Override
    public Cursor query(Uri uri, String[] projection, String selection,
                        String[] selectionArgs, String sortOrder) {
        if (uriMatcher.match(uri) != PREFERENCES) {
            throw new IllegalArgumentException("Unsupported URI: " + uri);
        }
        String packageName = uri.getPathSegments().get(1);
        if (TextUtils.isEmpty(packageName)){
            return null;
        }
        MatrixCursor result = new MatrixCursor(PROJECTION_DEFAULT);
        result.newRow().add(COLUMN_PROFILE, Preferences.getProfileId(getContext(), packageName));
        return result;
    }

    @Override
    public int delete(Uri uri, String selection, String[] selectionArgs) {
        return 0;
    }

    @Override
    public int update(Uri uri, ContentValues values, String selection,
                      String[] selectionArgs) {
        return 0;
    }

}
