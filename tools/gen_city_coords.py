#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# 中文：重新生成 main/services/city_coords.h。天气「定位」拿到 IP 城市名后先查这张表，
#       命中就不再发一次跨境地理编码请求（实测省约 1.9 秒）。
# English: Regenerate main/services/city_coords.h. The weather locate action looks up the
#          IP-derived city name here first, skipping one cross-border geocoding round trip
#          (measured at roughly 1.9 s).
#
# 数据来源 / Sources:
#   1) Open-Meteo 地理编码 geocoding-api.open-meteo.com（大部分城市）
#   2) OpenStreetMap Nominatim（Open-Meteo 未收录的地级市，仅离线生成时使用，
#      固件运行时不访问 Nominatim）
#
# 每条都按省份交叉校验 admin1/display_name，避免同音城市串味（如宿州与苏州）。
# Every row is cross-checked against its province so homophone cities cannot be mixed up.
#
# 用法 / Usage: python3 tools/gen_city_coords.py
import json
import time
import urllib.parse
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "main" / "services" / "city_coords.h"

# 省级行政区 -> 地级行政区（含直辖市与省会）
PROVINCES = {
    "直辖市": "北京 上海 天津 重庆",
    "河北": "石家庄 唐山 秦皇岛 邯郸 邢台 保定 张家口 承德 沧州 廊坊 衡水",
    "山西": "太原 大同 阳泉 长治 晋城 朔州 晋中 运城 忻州 临汾 吕梁",
    "内蒙古": "呼和浩特 包头 乌海 赤峰 通辽 鄂尔多斯 呼伦贝尔 巴彦淖尔 乌兰察布",
    "辽宁": "沈阳 大连 鞍山 抚顺 本溪 丹东 锦州 营口 阜新 辽阳 盘锦 铁岭 朝阳 葫芦岛",
    "吉林": "长春 吉林 四平 辽源 通化 白山 松原 白城",
    "黑龙江": "哈尔滨 齐齐哈尔 鸡西 鹤岗 双鸭山 大庆 伊春 佳木斯 七台河 牡丹江 黑河 绥化",
    "江苏": "南京 无锡 徐州 常州 苏州 南通 连云港 淮安 盐城 扬州 镇江 泰州 宿迁",
    "浙江": "杭州 宁波 温州 嘉兴 湖州 绍兴 金华 衢州 舟山 台州 丽水",
    "安徽": "合肥 芜湖 蚌埠 淮南 马鞍山 淮北 铜陵 安庆 黄山 滁州 阜阳 宿州 六安 亳州 池州 宣城",
    "福建": "福州 厦门 莆田 三明 泉州 漳州 南平 龙岩 宁德",
    "江西": "南昌 景德镇 萍乡 九江 新余 鹰潭 赣州 吉安 宜春 抚州 上饶",
    "山东": "济南 青岛 淄博 枣庄 东营 烟台 潍坊 济宁 泰安 威海 日照 临沂 德州 聊城 滨州 菏泽",
    "河南": "郑州 开封 洛阳 平顶山 安阳 鹤壁 新乡 焦作 濮阳 许昌 漯河 三门峡 南阳 商丘 信阳 周口 驻马店",
    "湖北": "武汉 黄石 十堰 宜昌 襄阳 鄂州 荆门 孝感 荆州 黄冈 咸宁 随州",
    "湖南": "长沙 株洲 湘潭 衡阳 邵阳 岳阳 常德 张家界 益阳 郴州 永州 怀化 娄底",
    "广东": "广州 韶关 深圳 珠海 汕头 佛山 江门 湛江 茂名 肇庆 惠州 梅州 汕尾 河源 阳江 清远 东莞 中山 潮州 揭阳 云浮",
    "广西": "南宁 柳州 桂林 梧州 北海 防城港 钦州 贵港 玉林 百色 贺州 河池 来宾 崇左",
    "海南": "海口 三亚 三沙 儋州",
    "四川": "成都 自贡 攀枝花 泸州 德阳 绵阳 广元 遂宁 内江 乐山 南充 眉山 宜宾 广安 达州 雅安 巴中 资阳",
    "贵州": "贵阳 六盘水 遵义 安顺 毕节 铜仁",
    "云南": "昆明 曲靖 玉溪 保山 昭通 丽江 普洱 临沧",
    "西藏": "拉萨 日喀则 昌都 林芝 山南 那曲",
    "陕西": "西安 铜川 宝鸡 咸阳 渭南 延安 汉中 榆林 安康 商洛",
    "甘肃": "兰州 嘉峪关 金昌 白银 天水 武威 张掖 平凉 酒泉 庆阳 定西 陇南",
    "青海": "西宁 海东",
    "宁夏": "银川 石嘴山 吴忠 固原 中卫",
    "新疆": "乌鲁木齐 克拉玛依 吐鲁番 哈密",
}


def norm(text):
    """去掉行政区后缀，便于跨源比较省份名。"""
    for suffix in ("省", "市", "自治区", "壮族", "回族", "维吾尔"):
        text = (text or "").replace(suffix, "")
    return text


def in_china(lat, lon):
    # 与固件 weather_location_valid 的框保持一致，框外结果一律丢弃。
    return lat is not None and lon is not None and 0 <= lat <= 54 and 73 <= lon <= 135


def open_meteo(name):
    url = ("https://geocoding-api.open-meteo.com/v1/search?name="
           f"{urllib.parse.quote(name)}&count=20&language=zh&format=json")
    for attempt in range(3):
        try:
            with urllib.request.urlopen(url, timeout=20) as response:
                payload = json.load(response)
            return [h for h in payload.get("results", [])
                    if h.get("country_code") == "CN"
                    and in_china(h.get("latitude"), h.get("longitude"))]
        except Exception:
            time.sleep(1.5 * (attempt + 1))
    return None


def nominatim(name):
    url = ("https://nominatim.openstreetmap.org/search?q="
           f"{urllib.parse.quote(name)}&format=jsonv2&limit=5&countrycodes=cn"
           "&accept-language=zh&addressdetails=1")
    request = urllib.request.Request(url, headers={"User-Agent": "kimiko-e4p/1.0 (offline table build)"})
    try:
        with urllib.request.urlopen(request, timeout=20) as response:
            return json.load(response)
    except Exception:
        return []


def resolve(name, province):
    """先查 Open-Meteo，再退回 Nominatim；两者的省份都必须对得上。"""
    want = norm(province)
    for variant in (name, name + "市"):
        for hit in open_meteo(variant) or []:
            shown = hit.get("name") or ""
            if not (shown == name or shown == name + "市" or shown.startswith(name)):
                continue
            admin1, admin2 = norm(hit.get("admin1")), norm(hit.get("admin2"))
            if province == "直辖市" or want in admin1 or want in admin2 \
                    or admin1.startswith(want) or want.startswith(admin1):
                return (hit["latitude"], hit["longitude"],
                        hit.get("admin1") or name, hit.get("admin2") or "")
    for query in (name, name + " 中国"):
        for hit in nominatim(query):
            try:
                lat, lon = float(hit["lat"]), float(hit["lon"])
            except (KeyError, TypeError, ValueError):
                continue
            if not in_china(lat, lon):
                continue
            address = hit.get("address", {})
            admin1 = address.get("state") or address.get("province") or ""
            admin2 = address.get("city") or address.get("town") or address.get("county") or ""
            if province == "直辖市" or want in norm(admin1) or want in norm(hit.get("display_name")):
                return (lat, lon, admin1 or name, admin2)
        time.sleep(1.2)
    return None


def main():
    rows, missing = [], []
    for province, names in PROVINCES.items():
        for name in names.split():
            hit = resolve(name, province)
            if hit:
                rows.append((name, hit[2], hit[3], hit[0], hit[1]))
            else:
                missing.append(name)
            time.sleep(0.15)

    rows.sort(key=lambda row: row[0])
    if missing:
        raise SystemExit(f"以下城市取不到坐标，修正后重跑：{missing}")

    lines = [
        "/* SPDX-License-Identifier: Apache-2.0",
        " * 中文：中国地级及以上城市的中心坐标与行政区，供天气定位直接取坐标，免去一次跨境地理编码。",
        " * English: Centre coordinates and administrative names for prefecture-level Chinese cities, so",
        " * the weather locate action can skip one cross-border geocoding round trip.",
        " *",
        " * 由 tools/gen_city_coords.py 生成，请勿手改；数据来源 Open-Meteo 地理编码与 OSM Nominatim，",
        " * 每条都按省份交叉校验过，避免同音城市串味（如宿州/苏州）。",
        " * Generated by tools/gen_city_coords.py; do not edit by hand. Sources are the Open-Meteo",
        " * geocoder and OSM Nominatim, each cross-checked against its province so homophone cities",
        " * such as 宿州/苏州 cannot be mixed up.",
        " */",
        "#pragma once",
        "typedef struct { const char *name, *admin1, *admin2; float lat, lon; } city_coord_t;",
        "// 按名称升序；查找为线性扫描，只在定位时调用一次。",
        "// Ascending by name; lookup is a linear scan used once per locate action.",
        "static const city_coord_t city_coords[] = {",
    ]
    for name, admin1, admin2, lat, lon in rows:
        lines.append(f'    {{"{name}", "{admin1}", "{admin2}", {lat:.4f}f, {lon:.4f}f}},')
    lines.append("};")
    OUT.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"写入 {OUT}，共 {len(rows)} 条")


if __name__ == "__main__":
    main()
