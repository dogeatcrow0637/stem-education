# code · reBot-DevArm 算法与实验

| 子目录（规划） | 主题 | 参考来源 | 状态 |
|---|---|---|---|
| `vision-grasp/` | 视觉抓取（YOLO/OBB + RGB-D） | [reBot-DevArm-Grasp](https://github.com/Seeed-Projects/reBot-DevArm-Grasp) | 待开始 |
| `vla-finetune/` | LeRobot 数据集 → GR00T N1.7 微调 → 真机部署 | 官方 GR00T 微调教程 | 待开始 |
| `trajectory/` | 正逆运动学、轨迹规划练习（教学用） | 自研 | 待开始 |
| `notebooks/` | 探索性实验与数据分析 | 自研 | 待开始 |

## 教学实验建议（可直接作为课程作业）

1. **运动学验证**：用 Pinocchio/URDF 计算某目标位姿的逆解，与真机读数对比误差
2. **抓取检测**：跑通 YOLO/OBB，统计不同光照/堆叠下的检测成功率
3. **数据采集对比**：同一任务采 20 / 40 / 80 条演示，观察微调后成功率变化
4. **泛化测试**：改变物体位置/颜色，评估策略泛化能力

## 约定

- 每个子目录一个 `README.md`：目的、输入、输出、运行命令、依赖
- **数据集与权重不入库**，在 `../resources/README.md` 登记来源、大小、SHA256
- 实验必须留下结论（哪个参数有效、指标多少），否则代码无法转化为教学素材
- 引用上游代码注明来源与 commit
