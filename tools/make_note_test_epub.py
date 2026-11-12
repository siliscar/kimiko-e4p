#!/usr/bin/env python3
"""中文：生成用于验证注释弹层的测试 EPUB（同章/跨章/各类角标/负例）。
English: Build the test EPUB that exercises the note sheet (same page, other page, marker
shapes and negatives).
"""
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT.parent / "note-test.epub"

LONG = "这是一条超长注释，用来确认弹层在两千字节处截断并补省略号。" * 40

CH1 = """<?xml version="1.0" encoding="utf-8"?>
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="zh-CN">
<head><title>第一章</title><link rel="stylesheet" href="style.css"/></head>
<body>
<h1>第一章 同页注释</h1>
<p>本页把常见的角标形态排在一起，注释都放在本章末尾。第一条是方括号数字<sup><a href="#n1">[1]</a></sup>，最普通的形式。</p>
<p>第二条是圆圈数字<sup><a href="#n2">①</a></sup>，日系译本常见。</p>
<p>第三条是上标数字<a href="#n3"><sup>3</sup></a>，标记本身只有数字。</p>
<p>第四条是星号<sup><a href="#n4">※</a></sup>。</p>
<p>第五条是「注N」写法<sup><a href="#n5">注1</a></sup>。</p>
<p>第六条是全角括号<sup><a href="#n6">［6］</a></sup>。</p>
<p>第七条是中文括号<sup><a href="#n7">〔7〕</a></sup>。</p>
<p>第八条故意写得很长<sup><a href="#n8">[8]</a></sup>，弹层应在两千字节处截断，末尾带省略号。</p>
<p>负例一：这一段是<strong>普通超链接</strong>，文字是词而不是标记——<a href="#n1">点这里查看第一条注释</a>。预期：不弹窗，按原来的翻页手势处理。</p>
<p>负例二：指向不存在的锚点<sup><a href="#nope">[9]</a></sup>。预期：不弹窗。</p>
<p>负例三：锚点落在段落中间<sup><a href="#mid">[10]</a></sup>。预期：不弹窗，否则只会显示半句话。</p>
<p>负例三的锚点就在这段中间：前面是正文<span id="mid">这里是锚点</span>后面还是正文。</p>
<h3>上标与下标渲染</h3>
<p>标签上标：正文<sup>123</sup>后面继续正文，行高不应变化；再跟一句足够长的话，用来看断行有没有因为小字而错位。</p>
<p>标签下标：H<sub>2</sub>O、x<sub>i</sub>、a<sub>1</sub>+b<sub>2</sub>，下标应当下沉而不是抬高。</p>
<p>行内样式：<span style="vertical-align:super;font-size:70%">上标</span> 与 <span style="font-size:70%">小字</span> 混排，位置应当与标签写法一致。</p>
<p>class 规则：<span class="sup">上标</span> 与 <span class="small">小字</span>，用来确认样式表里的声明也生效。</p>
<h3>注释</h3>
<p id="n1"><a href="#r1">[1]</a> 这是第一条注释。弹层会自动去掉行首的 [1] 返回标记，只显示正文。</p>
<p id="n2"><a href="#r2">①</a> 这是第二条注释，用来验证圆圈数字角标。</p>
<p id="n3"><a href="#r3">3</a> 这是第三条注释，角标是纯上标数字。</p>
<p id="n4"><a href="#r4">※</a> 这是第四条注释，角标是星号。</p>
<p id="n5"><a href="#r5">注1</a> 这是第五条注释，角标是「注」加数字。</p>
<p id="n6"><a href="#r6">［6］</a> 这是第六条注释，角标用全角方括号。</p>
<p id="n7"><a href="#r7">〔7〕</a> 这是第七条注释，角标用中文六角括号。</p>
<p id="n8"><a href="#r8">[8]</a> """ + LONG + """</p>
</body>
</html>
"""

CH2 = """<?xml version="1.0" encoding="utf-8"?>
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="zh-CN">
<head><title>第二章</title><link rel="stylesheet" href="style.css"/></head>
<body>
<h1>第二章 跨页注释</h1>
<p>这一章的注释不放在本章末尾，而是单独一页 text00003.html。点方括号数字<sup><a href="text00003.html#fn1">[1]</a></sup>应当直接弹出注释，阅读位置不变。</p>
<p>第二个跨页角标是圆圈数字<sup><a href="text00003.html#fn2">②</a></sup>，验证另一种形态也能跨页取到。</p>
<p>下面这段是普通正文，用来确认跨页取注释之后页码没有被改变：翻页、加书签、暂停再打开都应当还在这一页。</p>
</body>
</html>
"""

CH3 = """<?xml version="1.0" encoding="utf-8"?>
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="zh-CN">
<head><title>第二章注释</title><link rel="stylesheet" href="style.css"/></head>
<body>
<h1>第二章 注释</h1>
<p id="fn1"><a href="text00002.html#r1">[1]</a> 跨页注释第一条：这条注释在另一个 XHTML 里，设备需要临时解析本页才能取到它。</p>
<p id="fn2"><a href="text00002.html#r2">②</a> 跨页注释第二条：这一条开头没有返回标记，弹层同样只显示正文。</p>
</body>
</html>
"""

CONTAINER = """<?xml version="1.0" encoding="utf-8"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
  <rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles>
</container>
"""

OPF = """<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="bookid">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:title>注释测试</dc:title>
    <dc:creator>Kimiko</dc:creator>
    <dc:language>zh-CN</dc:language>
    <dc:identifier id="bookid">urn:uuid:6f1e2c40-9a3b-4d55-8a11-note00000001</dc:identifier>
  </metadata>
  <manifest>
    <item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>
    <item id="css" href="style.css" media-type="text/css"/>
    <item id="c1" href="text00001.html" media-type="application/xhtml+xml"/>
    <item id="c2" href="text00002.html" media-type="application/xhtml+xml"/>
    <item id="c3" href="text00003.html" media-type="application/xhtml+xml"/>
  </manifest>
  <spine toc="ncx">
    <itemref idref="c1"/>
    <itemref idref="c2"/>
    <itemref idref="c3"/>
  </spine>
</package>
"""

NCX = """<?xml version="1.0" encoding="utf-8"?>
<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">
  <head><meta name="dtb:uid" content="urn:uuid:6f1e2c40-9a3b-4d55-8a11-note00000001"/></head>
  <docTitle><text>注释测试</text></docTitle>
  <navMap>
    <navPoint id="p1" playOrder="1"><navLabel><text>第一章 同页注释</text></navLabel><content src="text00001.html"/></navPoint>
    <navPoint id="p2" playOrder="2"><navLabel><text>第二章 跨页注释</text></navLabel><content src="text00002.html"/></navPoint>
    <navPoint id="p3" playOrder="3"><navLabel><text>第二章 注释</text></navLabel><content src="text00003.html"/></navPoint>
  </navMap>
</ncx>
"""

STYLE = """.sup { vertical-align: super; font-size: 70%; }
.small { font-size: 80%; }
body { margin: 0; padding: 0; }
h1 { font-size: 1.4em; }
p { text-indent: 2em; line-height: 1.6; }
sup { font-size: 0.7em; vertical-align: super; }
"""

files = [
    ("OEBPS/content.opf", OPF),
    ("OEBPS/toc.ncx", NCX),
    ("OEBPS/style.css", STYLE),
    ("OEBPS/text00001.html", CH1),
    ("OEBPS/text00002.html", CH2),
    ("OEBPS/text00003.html", CH3),
    ("META-INF/container.xml", CONTAINER),
]

OUT.parent.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(OUT, "w") as z:
    info = zipfile.ZipInfo("mimetype")
    info.compress_type = zipfile.ZIP_STORED
    z.writestr(info, "application/epub+zip")
    for name, text in files:
        z.writestr(name, text.encode("utf-8"), compress_type=zipfile.ZIP_DEFLATED)
print(f"{OUT} {OUT.stat().st_size} bytes")
