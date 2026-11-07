from pathlib import Path
from functools import lru_cache
from collections import Counter
import csv, json
root=Path(__file__).resolve().parents[1]
files=set()
objects={}
parse_errors=[]
for p in (root/"data"/"json").rglob("*.json"):
    try:
        val=json.loads(p.read_text(encoding="utf-8-sig"))
    except Exception as exc:
        parse_errors.append((str(p),str(exc)))
        continue
    if isinstance(val,list):
        for obj in val:
            if isinstance(obj,dict) and obj.get("type")=="MONSTER":
                key=obj.get("id") or obj.get("abstract")
                if isinstance(key,str):
                    objects[key]=obj
                    if "id" in obj: files.add(str(p.relative_to(root)))
@lru_cache(None)
def effective(key):
    obj=objects.get(key,{})
    parent=obj.get("copy-from")
    result=dict(effective(parent)) if isinstance(parent,str) and parent in objects and parent!=key else {}
    result.update({k:v for k,v in obj.items() if k not in ("extend","delete")})
    ext=obj.get("extend",{})
    if isinstance(ext,dict) and isinstance(ext.get("species"),list):
        result["species"]=list(dict.fromkeys(result.get("species",[])+ext["species"]))
    return result

def cname(spec):
    s=set(spec)
    priorities=[("丧尸",{"ZOMBIE"}),("真菌",{"FUNGUS"}),("植物",{"PLANT","LEECH_PLANT"}),
       ("机器人与改造体",{"ROBOT","CYBORG","ROBOT_FLYING"}),("异界与恐怖",{"NETHER","NETHER_EMANATION","MIGO","HORROR","ABERRATION"}),
       ("变异生物",{"MUTANT"}),("黏液与异常体",{"BLOB","SLIME","HALLUCINATION"}),
       ("昆虫与蛛类",{"INSECT","INSECT_FLYING","CENTIPEDE","SPIDER","WORM"}),("动物",{"MAMMAL","BIRD","FISH","REPTILE","AMPHIBIAN","MOLLUSK"}),
       ("人类与狂暴者",{"HUMAN","FERAL"})]
    for group,types in priorities:
        if s&types:return group
    return "其他"

def get_name(n):
    if isinstance(n,dict):return n.get("str",n.get("str_sp",""))
    return n if isinstance(n,str) else ""

rows=[]
for mid,obj in objects.items():
    if "id" not in obj:continue
    item=effective(mid)
    species=item.get("species",[])
    if not isinstance(species,list):species=[]
    rows.append({"id":mid,"english_name":get_name(item.get("name")),
      "category":cname(species),"species":", ".join(species),
      "hp":item.get("hp",""),"speed":item.get("speed",""),
      "copy_from":str(obj.get("copy-from",""))})
rows.sort(key=lambda x:(x["category"],x["id"]))
dest=root/"docs"/"MONSTER_CATALOG_0I1.csv"
with dest.open("w",encoding="utf-8-sig",newline="") as f:
    w=csv.DictWriter(f,fieldnames=list(rows[0]))
    w.writeheader()
    w.writerows(rows)
cnt=Counter(row["category"] for row in rows)
spcnt=Counter(x for row in rows for x in row["species"].split(", ") if x)
lines=["# CDDA 0.I-1 怪物数据清单与图鉴","",
  "统计日期：2026-11-06。基于仓库默认 data/json 里 type=MONSTER 且带 id 的定义；abstract 原型不计入，未启用模组不计入。","",
  "## 默认游戏怪物总览","",
  f"怪物 ID 共 **{len(rows)}** 个，涉及 {len(files)} 份 JSON 文件；JSON 解析错误 {len(parse_errors)} 份。",
  "同一怪物可有多个 species，以下采用展示优先级互斥分类。部分属性来自 copy-from 简化继承解析；运行中以游戏完成加载的最终 mtype 为准。","",
  "| 分类 | 数量 | 示例 |","| --- | ---: | --- |"]
for c,n in cnt.most_common():
    names=[x["english_name"] for x in rows if x["category"]==c and x["english_name"]][:4]
    lines.append("| "+c+" | "+str(n)+" | "+"、".join(names)+" |")
lines.extend(["","## Species 交叉计数","",
              "| Species | 数量 |","| --- | ---: |"])
for s,n in spcnt.most_common():lines.append(f"| {s} | {n} |")
lines.extend(["","## 已实现图鉴范围","",
 "- 使用统一 Tab 生存菜单的新图鉴页，兼容 2D/3D 现代操作；原版经典模式不动。",
 "- 类型清单从运行时 MonsterGenerator 中读取，自动包含当前加载世界的模组怪物，不维护写死名单。",
 "- 互斥分类、名称/原始 ID 搜索、详细属性、原版翻译文本、图块包怪物图像（缺少时显示占位）。",
 "- 查询不读取实体实时生命或地图位置，不触发行动，不写入存档。",
 "- 统计定义种类并非当前世界的刷怪种类，生成受怪物组、地图生成与模组规则限制。",
 "- CSV 中名称是未经翻译的游戏 JSON 原文；游戏中使用翻译后的 mtype 名称。",
 "",
 "## 后续可扩展","",
 "- 遭遇解锁/击杀统计需单独接入游戏日志与存档机制；刷新模组/切换世界时应重新建索引。",
 "- 当前未实现出生点和精确掉落概率；这些属性不在 MONSTER 本身全部定义，需参考生成表和战利品表。",
 "",
 "## 数据与参考","",
 "- data/json/monsters/、data/json/species.json、data/json 其他 MONSTER 定义文件。",
 "- doc/JSON/MONSTERS.md、doc/JSON/JSON_INFO.md。",
 "- 逐 ID 索引见 docs/MONSTER_CATALOG_0I1.csv。",""])
(root/"docs"/"FP3D_BESTIARY.md").write_text("\n".join(lines),encoding="utf-8")
print("TOTAL",len(rows),"FILES",len(files),"ERRORS",len(parse_errors))
for c,n in cnt.most_common():print(c,n)
if parse_errors:print(parse_errors[:3])
