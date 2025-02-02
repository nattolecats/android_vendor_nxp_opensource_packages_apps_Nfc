/*
 * Copyright (C) 2013 The Android Open Source Project
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
/******************************************************************************
*
*  The original Work has been changed by NXP.
*
*  Licensed under the Apache License, Version 2.0 (the "License");
*  you may not use this file except in compliance with the License.
*  You may obtain a copy of the License at
*
*  http://www.apache.org/licenses/LICENSE-2.0
*
*  Unless required by applicable law or agreed to in writing, software
*  distributed under the License is distributed on an "AS IS" BASIS,
*  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*  See the License for the specific language governing permissions and
*  limitations under the License.
*
*  Copyright 2018-2021,2023 NXP
*
******************************************************************************/
package com.android.nfc.cardemulation;

import android.sysprop.NfcProperties;
import android.util.Log;
import android.util.SparseArray;
import android.content.Context;
import android.app.ActivityThread;
import android.app.ActivityManager;
import android.app.ActivityManager.RunningTaskInfo;
import com.android.nfc.NfcService;
import com.android.nfc.NfcStatsLog;
import android.util.SparseArray;
import android.util.proto.ProtoOutputStream;
import java.io.FileDescriptor;
import java.io.PrintWriter;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Hashtable;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.List;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Hashtable;
public class AidRoutingManager {

    static final String TAG = "AidRoutingManager";

    static final boolean DBG = NfcProperties.debug_enabled().orElse(false);

    static final int ROUTE_HOST = 0x00;

    // Every routing table entry is matched exact
    static final int AID_MATCHING_EXACT_ONLY = 0x00;
    // Every routing table entry can be matched either exact or prefix
    static final int AID_MATCHING_EXACT_OR_PREFIX = 0x01;
    // Every routing table entry is matched as a prefix
    static final int AID_MATCHING_PREFIX_ONLY = 0x02;
    // Every routing table entry can be matched either exact or prefix or subset only
    static final int AID_MATCHING_EXACT_OR_SUBSET_OR_PREFIX = 0x03;

    int mDefaultIsoDepRoute;
    //Let mDefaultRoute as default aid route
    int mDefaultRoute;

    int mMaxAidRoutingTableSize;
    int mDefaultAidRoute;
    final byte[] mOffHostRouteUicc;
    final byte[] mOffHostRouteEse;
    // Used for backward compatibility in case application doesn't specify the
    // SE
    int mDefaultOffHostRoute;

    // How the NFC controller can match AIDs in the routing table;
    // see AID_MATCHING constants
    final int mAidMatchingSupport;
    private int mAidRoutingTableSize;
    // Maximum AID routing table size
    final Object mLock = new Object();
    //set the status of last AID routes commit to routing table
    //if true, last commit was successful,
    //if false, there was an overflow of routing table for commit using last set of AID's in (mRouteForAid)
    boolean mLastCommitStatus;
    // if an Application is uninstalled and its AIDs are removed. In this case
    // if the AID route location is same as Default AID Route, then no routing update
    // is needed and this variable is set to false. Otherwise set to true
    // mAidRoutingTable contains the current routing table. The index is the route ID.
    // The route can include routes to a eSE/UICC.
    SparseArray<Set<String>> mAidRoutingTable =
            new SparseArray<Set<String>>();

    // Easy look-up what the route is for a certain AID
    HashMap<String, AidEntry> mRouteForAid = new HashMap<String, AidEntry>();

    private native int doGetDefaultRouteDestination();
    private native int doGetDefaultOffHostRouteDestination();
    private native byte[] doGetOffHostUiccDestination();
    private native byte[] doGetOffHostEseDestination();
    private native int doGetAidMatchingMode();
    private native int doGetDefaultIsoDepRouteDestination();
    final ActivityManager mActivityManager;
    final class AidEntry {
        boolean isOnHost;
        String offHostSE;
        int route;
        int aidInfo;
        int power;
    }

    public AidRoutingManager() {
        mDefaultRoute = doGetDefaultRouteDestination();
        if (DBG)
          Log.d(TAG, "mDefaultRoute=0x" + Integer.toHexString(mDefaultRoute));
        mDefaultOffHostRoute = doGetDefaultOffHostRouteDestination();
        if (DBG)
          Log.d(TAG, "mDefaultOffHostRoute=0x" + Integer.toHexString(mDefaultOffHostRoute));
        mOffHostRouteUicc = doGetOffHostUiccDestination();
        if (DBG)
            Log.d(TAG, "mOffHostRouteUicc=" + Arrays.toString(mOffHostRouteUicc));
        mOffHostRouteEse = doGetOffHostEseDestination();
        if (DBG)
          Log.d(TAG, "mOffHostRouteEse=" + Arrays.toString(mOffHostRouteEse));
        mAidMatchingSupport = doGetAidMatchingMode();
        if (DBG) Log.d(TAG, "mAidMatchingSupport=0x" + Integer.toHexString(mAidMatchingSupport));
        mDefaultAidRoute =   NfcService.getInstance().GetDefaultRouteEntry() >> 0x08;
        if (DBG)
          Log.d(TAG, "mDefaultAidRoute=0x" + Integer.toHexString(mDefaultAidRoute));
        mDefaultIsoDepRoute = doGetDefaultIsoDepRouteDestination();
        if (DBG) Log.d(TAG, "mDefaultIsoDepRoute=0x" + Integer.toHexString(mDefaultIsoDepRoute));
        mLastCommitStatus = false;

        Context context = (Context) ActivityThread.currentApplication();
        mActivityManager = (ActivityManager) context.getSystemService(Context.ACTIVITY_SERVICE);
    }

    public boolean supportsAidPrefixRouting() {
        return mAidMatchingSupport == AID_MATCHING_EXACT_OR_PREFIX ||
                mAidMatchingSupport == AID_MATCHING_PREFIX_ONLY ||
                 mAidMatchingSupport == AID_MATCHING_EXACT_OR_SUBSET_OR_PREFIX;
    }

    public boolean supportsAidSubsetRouting() {
        return mAidMatchingSupport == AID_MATCHING_EXACT_OR_SUBSET_OR_PREFIX;
    }

    public int calculateAidRouteSize(HashMap<String, AidEntry> routeCache) {
        // TAG + ROUTE + LENGTH_BYTE + POWER
        int AID_HDR_LENGTH = 0x04;
        int routeTableSize = 0x00;
        for(Map.Entry<String, AidEntry> aidEntry : routeCache.entrySet()) {
            String aid = aidEntry.getKey();
            // removing prefix length
            if(aid.endsWith("*")) {
                routeTableSize += ((aid.length() - 0x01) / 0x02) + AID_HDR_LENGTH;
            } else {
                routeTableSize += (aid.length() / 0x02)+ AID_HDR_LENGTH;
            }
        }
        if (DBG) Log.d(TAG, "calculateAidRouteSize: " + routeTableSize);
        return routeTableSize;
    }

    private void clearNfcRoutingTableLocked() {
        for (Map.Entry<String, AidEntry> aidEntry : mRouteForAid.entrySet())  {
            String aid = aidEntry.getKey();
            int route = aidEntry.getValue().route;
            if (aid.endsWith("*")) {
                if (mAidMatchingSupport == AID_MATCHING_EXACT_ONLY) {
                    Log.e(TAG, "Device does not support prefix AIDs but AID [" + aid
                            + "] is registered");
                } else if (mAidMatchingSupport == AID_MATCHING_PREFIX_ONLY) {
                    if (DBG) Log.d(TAG, "Unrouting prefix AID " + aid);
                    // Cut off '*' since controller anyway treats all AIDs as a prefix
                    aid = aid.substring(0, aid.length() - 1);
                } else if (mAidMatchingSupport == AID_MATCHING_EXACT_OR_PREFIX ||
                    mAidMatchingSupport == AID_MATCHING_EXACT_OR_SUBSET_OR_PREFIX) {
                    aid = aid.substring(0, aid.length() - 1);
                    if (DBG) Log.d(TAG, "Unrouting prefix AID " + aid);
                }
            }  else if (aid.endsWith("#")) {
                if (mAidMatchingSupport == AID_MATCHING_EXACT_ONLY) {
                    Log.e(TAG, "Device does not support subset AIDs but AID [" + aid
                            + "] is registered");
                } else if (mAidMatchingSupport == AID_MATCHING_PREFIX_ONLY ||
                    mAidMatchingSupport == AID_MATCHING_EXACT_OR_PREFIX) {
                    Log.e(TAG, "Device does not support subset AIDs but AID [" + aid
                            + "] is registered");
                } else if (mAidMatchingSupport == AID_MATCHING_EXACT_OR_SUBSET_OR_PREFIX) {
                    if (DBG) Log.d(TAG, "Unrouting subset AID " + aid);
                    aid = aid.substring(0, aid.length() - 1);
                }
            } else {
                if (DBG) Log.d(TAG, "Unrouting exact AID " + aid);
            }

            NfcService.getInstance().unrouteAids(aid);
        }
        if (NfcService.getInstance().getNciVersion() >= NfcService.getInstance().NCI_VERSION_2_0) {
            // unRoute EmptyAid
            NfcService.getInstance().unrouteAids("");
        }
    }

    private int getRouteForSecureElement(String se) {
        if (se == null || se.length() <= 3) {
            return 0;
        }
        try {
            if (se.startsWith("eSE") && mOffHostRouteEse != null) {
                int index = Integer.parseInt(se.substring(3));
                if (mOffHostRouteEse.length >= index && index > 0) {
                    return mOffHostRouteEse[index - 1] & 0xFF;
                }
            } else if (se.startsWith("SIM") && mOffHostRouteUicc != null) {
                int index = Integer.parseInt(se.substring(3));
                if (mOffHostRouteUicc.length >= index && index > 0) {
                    return mOffHostRouteUicc[index - 1] & 0xFF;
                }
            }
            if (mOffHostRouteEse == null && mOffHostRouteUicc == null)
              return mDefaultOffHostRoute;
        } catch (NumberFormatException e) { }
        return 0;
    }

    //check if Routing table update is needed for change in power state
    //or route location after conflict resolution.
    private boolean isRouteUpdateNeededAfterConflictRes(HashMap<String, AidEntry> currRouteForAid,
                                       Map.Entry<String, AidEntry> aidEntry){
        if((currRouteForAid.containsKey(aidEntry.getKey())) &&
            ((currRouteForAid.get(aidEntry.getKey()).route != aidEntry.getValue().route) ||
            (currRouteForAid.get(aidEntry.getKey()).power != aidEntry.getValue().power))){
                return true;
            }
        return false;
    }

    //Check if Any AID entry needs to be removed from previously registered
    //entries in the Routing table. Current AID entries are part of
    //mRouteForAid and previously registered AID entries are part of input
    //argument prevRouteForAid.
    private boolean checkUnrouteAid(HashMap<String, AidEntry> prevRouteForAid) {
        for (Map.Entry<String, AidEntry> aidEntry : prevRouteForAid.entrySet())  {
            if((aidEntry.getValue().route != mDefaultAidRoute) &&
                (!mRouteForAid.containsKey(aidEntry.getKey()) ||
                isRouteUpdateNeededAfterConflictRes(mRouteForAid, aidEntry))){
                    return true;
            }
        }
        return false;
    }

    //Check if Any AID entry needs to be added to previously registered
    //entries in the Routing table. Current AID entries are part of
    //mRouteForAid and previously registered AID entries are part of input
    //argument prevRouteForAid.
    private boolean checkRouteAid(HashMap<String, AidEntry> prevRouteForAid){
        for (Map.Entry<String, AidEntry> aidEntry : mRouteForAid.entrySet())  {
            if((aidEntry.getValue().route != mDefaultAidRoute) &&
                (!prevRouteForAid.containsKey(aidEntry.getKey())||
                isRouteUpdateNeededAfterConflictRes(prevRouteForAid, aidEntry))){
                    return true;
            }
        }
        return false;
    }

    public boolean configureRouting(HashMap<String, AidEntry> aidMap, boolean force) {
        boolean aidRouteResolved = false;
        HashMap<String, AidEntry> aidRoutingTableCache = new HashMap<String, AidEntry>(aidMap.size());
        ArrayList<Integer> seList = new ArrayList<Integer>();
        mAidRoutingTableSize = NfcService.getInstance().getAidRoutingTableSize();
        mDefaultAidRoute =   NfcService.getInstance().GetDefaultRouteEntry() >> 0x08;
        mDefaultOffHostRoute = doGetDefaultOffHostRouteDestination();
        boolean isPowerStateUpdated = false;
        Log.e(TAG, "Size of routing table"+mAidRoutingTableSize);
        seList.add(mDefaultAidRoute);
        if (mDefaultRoute != ROUTE_HOST) {
            seList.add(ROUTE_HOST);
        }
        SparseArray<Set<String>> aidRoutingTable = new SparseArray<Set<String>>(aidMap.size());
        HashMap<String, AidEntry> routeForAid = new HashMap<String, AidEntry>(aidMap.size());
        HashMap<String, Integer> infoForAid = new HashMap<String, Integer>(aidMap.size());
        HashMap<String, Integer> powerForAid = new HashMap<String, Integer>(aidMap.size());
        HashMap<String, AidEntry> prevRouteForAid = new HashMap<String, AidEntry>();

        // Then, populate internal data structures first
        for (Map.Entry<String, AidEntry> aidEntry : aidMap.entrySet())  {
            int route = ROUTE_HOST;
            if (!aidEntry.getValue().isOnHost) {
                String offHostSE = aidEntry.getValue().offHostSE;
                if (offHostSE == null) {
                    route = mDefaultOffHostRoute;
                } else {
                    route = getRouteForSecureElement(offHostSE);
                    if (route == 0) {
                        Log.e(TAG, "Invalid Off host Aid Entry " + offHostSE);
                        continue;
                    }
                }
            }
            if (!seList.contains(route))
                seList.add(route);
            aidEntry.getValue().route = route;
            int aidType = aidEntry.getValue().aidInfo;
            int power = aidEntry.getValue().power;
            String aid = aidEntry.getKey();
            Set<String> entries =
                    aidRoutingTable.get(route, new HashSet<String>());
            entries.add(aid);
            aidRoutingTable.put(route, entries);
            routeForAid.put(aid, aidMap.get(aid));
            infoForAid.put(aid, aidType);
            powerForAid.put(aid, power);
            if (DBG) Log.d(TAG, "#######Routing AID " + aid + " to route "
                        + Integer.toString(route) + " with power "+ power);
        }
        if (!seList.contains(ROUTE_HOST))
          seList.add(ROUTE_HOST);

        synchronized (mLock) {
            mLastCommitStatus = false;
            if (routeForAid.equals(mRouteForAid) && !force) {
                NfcService.getInstance().addT4TNfceeAid();
                if (DBG) Log.d(TAG, "Routing table unchanged, not updating");
                return false;
            }

            // Otherwise, update internal structures and commit new routing
            clearNfcRoutingTableLocked();
            NfcService.getInstance().addT4TNfceeAid();
            prevRouteForAid = mRouteForAid;
            mRouteForAid = routeForAid;
            mAidRoutingTable = aidRoutingTable;
            mMaxAidRoutingTableSize = NfcService.getInstance().getAidRoutingTableSize();
            if (DBG) Log.d(TAG, "mMaxAidRoutingTableSize: " + mMaxAidRoutingTableSize);
            mDefaultRoute = mDefaultAidRoute;
            for(int index=0; index < seList.size(); index++) {
                mDefaultRoute = seList.get(index);
                if(index != 0) {
                    if (DBG) {
                        Log.d(TAG, "AidRoutingTable is full, try to switch mDefaultRoute to 0x" + Integer.toHexString(mDefaultRoute));
                    }
                }

                aidRoutingTableCache.clear();

                if (mAidMatchingSupport == AID_MATCHING_PREFIX_ONLY) {
                    /* If a non-default route registers an exact AID which is shorter
                     * than this exact AID, this will create a problem with controllers
                     * that treat every AID in the routing table as a prefix.
                     * For example, if App A registers F0000000041010 as an exact AID,
                     * and App B registers F000000004 as an exact AID, and App B is not
                     * the default route, the following would be added to the routing table:
                     * F000000004 -> non-default destination
                     * However, because in this mode, the controller treats every routing table
                     * entry as a prefix, it means F0000000041010 would suddenly go to the non-default
                     * destination too, whereas it should have gone to the default.
                     *
                     * The only way to prevent this is to add the longer AIDs of the
                     * default route at the top of the table, so they will be matched first.
                     */
                    Set<String> defaultRouteAids = mAidRoutingTable.get(mDefaultRoute);
                    if (defaultRouteAids != null) {
                        for (String defaultRouteAid : defaultRouteAids) {
                            // Check whether there are any shorted AIDs routed to non-default
                            // TODO this is O(N^2) run-time complexity...
                            for (Map.Entry<String, AidEntry> aidEntry : mRouteForAid.entrySet()) {
                                String aid = aidEntry.getKey();
                                int route = aidEntry.getValue().route;
                                if (defaultRouteAid.startsWith(aid) && route != mDefaultRoute) {
                                    if (DBG) Log.d(TAG, "Adding AID " + defaultRouteAid + " for default " +
                                            "route, because a conflicting shorter AID will be " +
                                            "added to the routing table");
                                     aidRoutingTableCache.put(defaultRouteAid, aidMap.get(defaultRouteAid));
                                }
                            }
                        }
                    }
                }

                // Add AID entries for all non-default routes
                for (int i = 0; i < mAidRoutingTable.size(); i++) {
                    int route = mAidRoutingTable.keyAt(i);
                    if (route != mDefaultRoute) {
                        Set<String> aidsForRoute = mAidRoutingTable.get(route);
                        for (String aid : aidsForRoute) {
                            if (aid.endsWith("*")) {
                                if (mAidMatchingSupport == AID_MATCHING_EXACT_ONLY) {
                                    Log.e(TAG, "This device does not support prefix AIDs.");
                                } else if (mAidMatchingSupport == AID_MATCHING_PREFIX_ONLY) {
                                    if (DBG) Log.d(TAG, "Routing prefix AID " + aid + " to route "
                                            + Integer.toString(route));
                                    // Cut off '*' since controller anyway treats all AIDs as a prefix
                                    aidRoutingTableCache.put(aid.substring(0,aid.length() - 1), aidMap.get(aid));
                                } else if (mAidMatchingSupport == AID_MATCHING_EXACT_OR_PREFIX ||
                                  mAidMatchingSupport == AID_MATCHING_EXACT_OR_SUBSET_OR_PREFIX) {
                                    if (DBG) Log.d(TAG, "Routing prefix AID " + aid + " to route "
                                            + Integer.toString(route));
                                     aidRoutingTableCache.put(aid.substring(0,aid.length() - 1), aidMap.get(aid));
                                }
                            } else if (aid.endsWith("#")) {
                                if (mAidMatchingSupport == AID_MATCHING_EXACT_ONLY) {
                                    Log.e(TAG, "Device does not support subset AIDs but AID [" + aid
                                            + "] is registered");
                                } else if (mAidMatchingSupport == AID_MATCHING_PREFIX_ONLY ||
                                    mAidMatchingSupport == AID_MATCHING_EXACT_OR_PREFIX) {
                                    Log.e(TAG, "Device does not support subset AIDs but AID [" + aid
                                            + "] is registered");
                                } else if (mAidMatchingSupport == AID_MATCHING_EXACT_OR_SUBSET_OR_PREFIX) {
                                    if (DBG) Log.d(TAG, "Routing subset AID " + aid + " to route "
                                            + Integer.toString(route));
                                    aidRoutingTableCache.put(aid.substring(0,aid.length() - 1), aidMap.get(aid));
                                }
                            } else {
                                if (DBG) Log.d(TAG, "Routing exact AID " + aid + " to route "
                                        + Integer.toString(route));
                                aidRoutingTableCache.put(aid, aidMap.get(aid));
                            }
                         }
                    }
                }

                // register default route in below cases:
                // 1. mDefaultRoute is different with mDefaultIsoDepRoute
                // 2. mDefaultRoute and mDefaultIsoDepRoute all equal to ROUTE_HOST
                //    , which is used for screen off HCE scenarios
                if (mDefaultRoute != mDefaultIsoDepRoute || mDefaultIsoDepRoute == ROUTE_HOST) {
                    if (NfcService.getInstance().getNciVersion()
                            >= NfcService.getInstance().NCI_VERSION_2_0) {
                        String emptyAid = "";
                        AidEntry entry = new AidEntry();
                        int default_route_power_state;
                        entry.route = mDefaultRoute;
                        if (mDefaultRoute == ROUTE_HOST) {
                            entry.isOnHost = true;
                            default_route_power_state = RegisteredAidCache.POWER_STATE_SWITCH_ON
                                    | RegisteredAidCache.POWER_STATE_SCREEN_ON_LOCKED;
                        } else {
                            entry.isOnHost = false;
                            default_route_power_state = RegisteredAidCache.POWER_STATE_ALL;
                        }
                        entry.aidInfo = RegisteredAidCache.AID_ROUTE_QUAL_PREFIX;
                        entry.power = default_route_power_state;

                        aidRoutingTableCache.put(emptyAid, entry);
                        if (DBG) Log.d(TAG, "Add emptyAid into AidRoutingTable");
                    }
                }

                // Register additional offhost AIDs when their support power states are
                // differernt from the default route entry
                if (mDefaultRoute != ROUTE_HOST) {
                    int default_route_power_state = RegisteredAidCache.POWER_STATE_ALL;
                    default_route_power_state &= ~RegisteredAidCache.POWER_STATE_BATTERY_OFF;
                    if (NfcService.getInstance().getNciVersion()
                            < NfcService.getInstance().NCI_VERSION_2_0) {
                        default_route_power_state =
                                RegisteredAidCache.POWER_STATE_ALL_NCI_VERSION_1_0;
                    }

                    Set<String> aidsForDefaultRoute = mAidRoutingTable.get(mDefaultRoute);
                    if (aidsForDefaultRoute != null) {
                        for (String aid : aidsForDefaultRoute) {
                            if (aidMap.get(aid).power != default_route_power_state) {
                                if(aid.endsWith("*") || aid.endsWith("#"))
                                    aidRoutingTableCache.put(aid.substring(0,aid.length() - 1), aidMap.get(aid));
                                else
                                    aidRoutingTableCache.put(aid, aidMap.get(aid));
                                isPowerStateUpdated = true;
                            }
                        }
                    }
                }

                if (calculateAidRouteSize(aidRoutingTableCache) <= mMaxAidRoutingTableSize) {
                    aidRouteResolved = true;
                    break;
                }
            }

            boolean mIsUnrouteRequired = checkUnrouteAid(prevRouteForAid);
            boolean isRouteTableUpdated = checkRouteAid(prevRouteForAid);

            if (isPowerStateUpdated || isRouteTableUpdated || mIsUnrouteRequired || force) {
                if (aidRouteResolved) {
                    NfcService.getInstance().updateDefaultAidRoute(mDefaultRoute);
                    mLastCommitStatus = true;
                    commit(aidRoutingTableCache);
                } else {
                    NfcStatsLog.write(NfcStatsLog.NFC_ERROR_OCCURRED,
                            NfcStatsLog.NFC_ERROR_OCCURRED__TYPE__AID_OVERFLOW, 0, 0);
                    Log.e(TAG, "RoutingTable unchanged because it's full, not updating");
                    NfcService.getInstance().notifyRoutingTableFull();
                    mLastCommitStatus = false;
                }
            } else {
                Log.e(TAG, "All AIDs routing to mDefaultRoute, RoutingTable"
                        + " update is not required");
            }
        }
        return true;
    }

    private void commit(HashMap<String, AidEntry> routeCache ) {
       if(routeCache == null)
       {
         return;
       }
        for (Map.Entry<String, AidEntry> aidEntry : routeCache.entrySet())  {
          /*NXP_EXTNS: Empty Aid route is registered by Nfc service. To align majority of code with
           * AOSP, additional check is added to skip empty aid route registration from
           * AidRoutingManager*/
          if (aidEntry.getKey().isEmpty()) {
            continue;
          }
            int route = aidEntry.getValue().route;
            int aidType = aidEntry.getValue().aidInfo;
            String aid = aidEntry.getKey();
            int power = aidEntry.getValue().power;
            if (DBG) {
              Log.d(TAG,
                  "commit aid:" + aid + ",route:" + route + ",aidtype:" + aidType
                      + ", power state:" + power);
            }

            NfcService.getInstance().routeAids(aid, route, aidType, power);
        }

        if (NfcService.getInstance().isNfcEnabled())
          NfcService.getInstance().commitRouting();
    }
    /**
     * This notifies that the AID routing table in the controller
     * has been cleared (usually due to NFC being turned off).
     */
    public void onNfccRoutingTableCleared() {
        // The routing table in the controller was cleared
        // To stay in sync, clear our own tables.
        synchronized (mLock) {
            mAidRoutingTable.clear();
            mRouteForAid.clear();
        }
    }

    public boolean getLastCommitRoutingStatus() {
        synchronized (mLock) {
            return mLastCommitStatus;
        }
    }

    public void dump(FileDescriptor fd, PrintWriter pw, String[] args) {
        pw.println("Routing table:");
        pw.println("    Default route: " + ((mDefaultRoute == 0x00) ? "host" : "secure element"));
        synchronized (mLock) {
            for (int i = 0; i < mAidRoutingTable.size(); i++) {
                Set<String> aids = mAidRoutingTable.valueAt(i);
                pw.println("    Routed to 0x" + Integer.toHexString(mAidRoutingTable.keyAt(i)) + ":");
                for (String aid : aids) {
                    pw.println("        \"" + aid + "\"");
                }
            }
        }
    }

    // Returns true if AppChooserActivity is foreground to restart RF discovery so that
    // TapAgainDialog is dismissed when an external reader detects the device.
    private boolean isProcessingTapAgain() {
        String appChooserActivityClassName = AppChooserActivity.class.getName();
        return appChooserActivityClassName.equals(getTopClass());
    }

    private String getTopClass() {
        String topClass = null;
        List<RunningTaskInfo> tasks = mActivityManager.getRunningTasks(1);
        if (tasks != null && tasks.size() > 0) {
            topClass = tasks.get(0).topActivity.getClassName();
        }
        return topClass;
    }

    /**
     * Dump debugging information as a AidRoutingManagerProto
     *
     * Note:
     * See proto definition in frameworks/base/core/proto/android/nfc/card_emulation.proto
     * When writing a nested message, must call {@link ProtoOutputStream#start(long)} before and
     * {@link ProtoOutputStream#end(long)} after.
     * Never reuse a proto field number. When removing a field, mark it as reserved.
     */
    void dumpDebug(ProtoOutputStream proto) {
        proto.write(AidRoutingManagerProto.DEFAULT_ROUTE, mDefaultRoute);
        synchronized (mLock) {
            for (int i = 0; i < mAidRoutingTable.size(); i++) {
                long token = proto.start(AidRoutingManagerProto.ROUTES);
                proto.write(AidRoutingManagerProto.Route.ID, mAidRoutingTable.keyAt(i));
                mAidRoutingTable.valueAt(i).forEach(aid -> {
                    proto.write(AidRoutingManagerProto.Route.AIDS, aid);
                });
                proto.end(token);
            }
        }
    }
}
