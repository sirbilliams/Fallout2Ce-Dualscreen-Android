package com.alexbatalov.fallout2ce;

import android.app.DownloadManager;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

// The system's download broadcasts: a finished download updates the index;
// a tap on the notification opens the Downloads screen.
public class ModDownloadReceiver extends BroadcastReceiver {
    @Override
    public void onReceive(Context context, Intent intent) {
        String action = intent.getAction();
        if (DownloadManager.ACTION_DOWNLOAD_COMPLETE.equals(action)) {
            ModDownloads.rows(context);  // stores the final state in the index
        } else if (DownloadManager.ACTION_NOTIFICATION_CLICKED.equals(action)) {
            Intent open = new Intent(context, DownloadsActivity.class);
            open.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            context.startActivity(open);
        }
    }
}
