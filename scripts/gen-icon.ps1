# gen-icon.ps1 —— WhoLock 扁平图标生成（v1.0 F9）
# 口径：纯扁平无投影、品牌蓝实心、白色雅黑"锁"字；输出 src/app.ico 多尺寸 PNG 条目。
# 实现走 Add-Type C# 内联（规避 PowerShell 5.1 的 GDI/流互操作怪癖）。
$ErrorActionPreference = 'Stop'
$src = @'
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Drawing.Text;
using System.IO;

public static class IconGen {
    public static void Generate(string outPath) {
        int[] sizes = { 16, 24, 32, 48, 64, 128, 256 };
        var pngs = new List<byte[]>();
        foreach (int s in sizes) {
            using (var bmp = new Bitmap(s, s))
            using (var g = Graphics.FromImage(bmp)) {
                g.SmoothingMode = SmoothingMode.AntiAlias;
                g.TextRenderingHint = TextRenderingHint.AntiAliasGridFit;
                using (var bg = new SolidBrush(Color.FromArgb(255, 37, 99, 235)))
                    g.FillRectangle(bg, 0, 0, s, s); // 品牌蓝实心直角方：扁平、无投影
                float fs = Math.Max(8f, s * 0.58f);
                using (var font = new Font("Microsoft YaHei", fs, FontStyle.Bold, GraphicsUnit.Pixel))
                using (var sf = new StringFormat {
                    Alignment = StringAlignment.Center,
                    LineAlignment = StringAlignment.Center
                }) {
                    g.DrawString("\u9501", font, Brushes.White, new RectangleF(0, -s * 0.04f, s, s), sf);
                }
                using (var ms = new MemoryStream()) {
                    bmp.Save(ms, ImageFormat.Png);
                    pngs.Add(ms.ToArray());
                }
            }
        }
        using (var fs = new FileStream(outPath, FileMode.Create))
        using (var bw = new BinaryWriter(fs)) {
            bw.Write((UInt16)0); bw.Write((UInt16)1); bw.Write((UInt16)pngs.Count);
            uint offset = (uint)(6 + 16 * pngs.Count);
            for (int i = 0; i < pngs.Count; i++) {
                int s = sizes[i];
                bw.Write((byte)(s >= 256 ? 0 : s));
                bw.Write((byte)(s >= 256 ? 0 : s));
                bw.Write((byte)0); bw.Write((byte)0);
                bw.Write((UInt16)1); bw.Write((UInt16)32);
                bw.Write((UInt32)pngs[i].Length);
                bw.Write(offset);
                offset += (uint)pngs[i].Length;
            }
            foreach (var d in pngs) bw.Write(d);
        }
        Console.WriteLine("icon written: " + new FileInfo(outPath).Length + " bytes, " + pngs.Count + " sizes");
    }
}
'@
Add-Type -TypeDefinition $src -Language CSharp -ReferencedAssemblies System.Drawing
$out = Join-Path $PSScriptRoot "..\src\app.ico"
[IconGen]::Generate((Resolve-Path (Join-Path $PSScriptRoot "..")).Path + "\src\app.ico")
