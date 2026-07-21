from ultralytics import YOLO

# 1. 加载模型
# 注意：这里加载的是 yolo26n.pt
model = YOLO("./yolo26n.pt")

if __name__ == '__main__':
    # 2. 开始训练
    results = model.train(
        data="helmet.yaml",           # 数据集配置文件路径
        epochs=300,                   # 建议将训练轮数提升至300，配合早停机制更合理
        imgsz=640,                    # 明确指定输入图像尺寸为640x640
        
        batch=-1,                     # 设置为-1开启AutoBatch，自动探测3060Ti能跑满的最大批次大小
        workers=4,                    # 设为4以充分利用CPU多线程，避免GPU等待数据
        
        patience=50,                  # 如果验证集指标连续50轮不提升则提前停止，防止过拟合
        device=0,                     # 明确指定使用第一张显卡进行训练
        
        task="detect",                # 任务类型：目标检测
        mode="train",                 # 模式：训练
        verbose=True                  # 显示详细训练日志
    )
    
    print("训练完成！")