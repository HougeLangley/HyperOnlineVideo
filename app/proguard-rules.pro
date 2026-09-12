# 《聚合视频》R8 规则

# ---- Intent extras 传递的 Serializable 模型（R8 改名会导致 getSerializableExtra 反序列化失败）----
-keep class com.hougelangley.hyperonlinevideo.data.QualityOption { *; }
-keep class com.hougelangley.hyperonlinevideo.data.SubTrack { *; }
-keep class com.hougelangley.hyperonlinevideo.data.VideoDetail { *; }
-keep class com.hougelangley.hyperonlinevideo.data.MediaFormat { *; }

# ---- libmpv（JNI 回调 + 原生库按类名查找）----
-keep class is.xyz.mpv.** { *; }
-keepclassmembers class is.xyz.mpv.** { *; }

# ---- youtubedl-android（Python 引导 + JNI）----
-keep class com.yausername.** { *; }
-keepclassmembers class com.yausername.** { *; }

# ---- Coil ----
-dontwarn okhttp3.**
-dontwarn okio.**

# ---- 保留行号，便于线上崩溃定位 ----
-keepattributes SourceFile,LineNumberTable
-renamesourcefileattribute SourceFile

# ---- commons-compress（youtubedl 解压 Python 包用；1.12 的静态初始化会反射扫描实现类，
#      R8 混淆后会抛 "class xx is not a concrete class" 导致 YoutubeDL.init 失败）----
-keep class org.apache.commons.compress.** { *; }
-dontwarn org.apache.commons.**
