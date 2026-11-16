# code · 算法与实验代码

按主题分子目录，例如：

```
code/
├── vision/          # 视觉识别、抓取检测
├── trajectory/      # 轨迹规划、运动学
├── vla-finetune/    # 模仿学习 / VLA 微调
└── notebooks/       # 探索性实验
```

## 约定

1. 每个子目录一个 `README.md`：目的、输入、输出、运行命令、依赖。
2. **数据集与权重不入库**：写明来源、版本、下载方式与校验值，放在 `resources/README.md` 登记。
3. 实验要有可复现的最小命令：`python train.py --config configs/xxx.yaml`。
4. 记录实验结论（哪个参数有效、指标多少），否则代码无法转化为教学素材。
5. 引用外部仓库代码时注明来源与 commit，不要整包复制。
