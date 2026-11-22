// 3FP 单文件原生载荷打包器（.NET 10+ 文件式应用，构建机用 dotnet run 调用）。
//
// 只压「本来就要被吐到磁盘」的那批原生件：.NET 的单文件 bundle 压缩只对自包含发布
// 成立（框架依赖发布被 SDK 拒绝，运行时的 FDD apphost 也拒绝压缩条目），所以压缩
// 放在传输层自己做，解压在宿主启动早期做。
//
// 格式（LittleEndian，整体 Brotli-压缩）：重复的
//   int32 名称字节数 | 名称(UTF-8) | int64 数据字节数 | 数据
// 读取侧是 FFF.Player\Core\原生载荷.vb，两边必须同步改。
//
// 用法：dotnet run native_payload.cs -- <输出文件> <清单文件>
// 清单文件每行一个绝对路径，由 FFF.Player.vbproj 的
// WriteSingleFileNativePayloadList target 生成（口径=单文件包该带的 16 件）。
using System.IO.Compression;
using System.Text;

if (args.Length != 2)
{
    Console.Error.WriteLine("用法：dotnet run native_payload.cs -- <输出文件> <清单文件>");
    return 2;
}

var listPath = Path.GetFullPath(args[1]);
var outputPath = Path.GetFullPath(args[0]);
var inputs = File.ReadAllLines(listPath)
    .Select(line => line.Trim())
    .Where(line => line.Length > 0)
    .Select(Path.GetFullPath)
    .ToArray();

if (inputs.Length == 0)
{
    Console.Error.WriteLine($"载荷清单为空：{listPath}");
    return 3;
}

var missing = inputs.Where(path => !File.Exists(path)).ToArray();
if (missing.Length > 0)
{
    Console.Error.WriteLine("载荷清单里有不存在的文件：" + string.Join("；", missing));
    return 4;
}

// 同一目录名重复 = 打包成同名条目，解包会互相覆盖；宁可在这里拦死。
var duplicates = inputs.GroupBy(Path.GetFileName, StringComparer.OrdinalIgnoreCase)
    .Where(group => group.Count() > 1).ToArray();
if (duplicates.Length > 0)
{
    Console.Error.WriteLine("载荷清单里有重名文件：" + string.Join("；", duplicates.Select(g => g.Key)));
    return 5;
}

var outputDirectory = Path.GetDirectoryName(outputPath);
if (!string.IsNullOrEmpty(outputDirectory)) Directory.CreateDirectory(outputDirectory);

using (var output = File.Create(outputPath))
using (var brotli = new BrotliStream(output, CompressionLevel.SmallestSize))
using (var writer = new BinaryWriter(brotli, Encoding.UTF8, leaveOpen: true))
{
    foreach (var path in inputs)
    {
        var name = Encoding.UTF8.GetBytes(Path.GetFileName(path));
        var data = File.ReadAllBytes(path);
        writer.Write(name.Length);
        writer.Write(name);
        writer.Write((long)data.Length);
        writer.Write(data);
    }
    writer.Flush();
}

Console.WriteLine($"原生载荷：{inputs.Length} 件 → {outputPath} {new FileInfo(outputPath).Length} 字节");
return 0;