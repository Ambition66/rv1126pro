# Git 操作说明

本文说明如何把当前文件夹 `rv1126` 推送到远程仓库、如何推送到分支，以及如何把分支合并回主分支。

当前仓库信息：

```powershell
本地目录：C:\Users\xinweis\OneDrive - Qualcomm\Desktop\rv1126
远程仓库：origin -> https://github.com/Ambition66/rv1126pro.git
主分支：main
```

## 1. 检查当前仓库状态

进入当前文件夹：

```powershell
cd "C:\Users\xinweis\OneDrive - Qualcomm\Desktop\rv1126"
```

查看当前分支和文件状态：

```powershell
git status
```

查看远程仓库地址：

```powershell
git remote -v
```

如果能看到类似下面的输出，说明已经绑定远程仓库：

```text
origin  https://github.com/Ambition66/rv1126pro.git (fetch)
origin  https://github.com/Ambition66/rv1126pro.git (push)
```

## 2. 把当前文件夹内容提交到本地仓库

添加当前文件夹下所有新增、修改、删除的文件：

```powershell
git add .
```

提交到本地仓库：

```powershell
git commit -m "Update project files"
```

如果提示 `nothing to commit`，说明当前没有需要提交的变更。

## 3. 推送到远程主分支 main

确认当前在 `main` 分支：

```powershell
git branch
```

如果当前不是 `main`，切换到 `main`：

```powershell
git switch main
```

推送到远程仓库的 `main` 分支：

```powershell
git push origin main
```

常用完整流程：

```powershell
git status
git add .
git commit -m "Update project files"
git push origin main
```

## 4. 推送到一个新分支

如果不想直接推到 `main`，可以先创建新分支，例如 `dev`：

```powershell
git switch -c dev
```

添加并提交修改：

```powershell
git add .
git commit -m "Update project files"
```

把本地 `dev` 分支推送到远程仓库：

```powershell
git push -u origin dev
```

参数说明：

- `origin`：远程仓库名称。
- `dev`：要推送的分支名称。
- `-u`：建立本地分支和远程分支的跟踪关系，以后在这个分支上可以直接使用 `git push`。

后续在 `dev` 分支继续提交时，可以直接执行：

```powershell
git add .
git commit -m "Update files"
git push
```

## 5. 切换已有分支

查看本地分支：

```powershell
git branch
```

查看本地和远程所有分支：

```powershell
git branch -a
```

切换到已有本地分支：

```powershell
git switch dev
```

如果远程已经有 `dev` 分支，但本地还没有，可以执行：

```powershell
git fetch origin
git switch dev
```

或者：

```powershell
git switch -c dev origin/dev
```

## 6. 把分支合并到 main

假设现在开发分支是 `dev`，要把 `dev` 合并回 `main`。

先切换到主分支：

```powershell
git switch main
```

拉取远程最新代码，避免本地 `main` 落后：

```powershell
git pull origin main
```

合并 `dev` 到 `main`：

```powershell
git merge dev
```

如果没有冲突，推送合并后的 `main`：

```powershell
git push origin main
```

完整流程：

```powershell
git switch main
git pull origin main
git merge dev
git push origin main
```

## 7. 处理合并冲突

如果执行 `git merge dev` 后出现冲突，Git 会提示哪些文件冲突。

查看冲突文件：

```powershell
git status
```

打开冲突文件后，会看到类似内容：

```text
<<<<<<< HEAD
main 分支上的内容
=======
dev 分支上的内容
>>>>>>> dev
```

手动保留正确内容，并删除 `<<<<<<<`、`=======`、`>>>>>>>` 这些标记。

解决完成后执行：

```powershell
git add .
git commit -m "Resolve merge conflicts"
git push origin main
```

## 8. 删除已经合并的分支

合并完成后，如果不再需要 `dev` 分支，可以删除本地分支：

```powershell
git branch -d dev
```

删除远程分支：

```powershell
git push origin --delete dev
```

如果分支还没有合并，`git branch -d dev` 可能会拒绝删除。确认不需要该分支后，可以强制删除本地分支：

```powershell
git branch -D dev
```

## 9. 推荐日常流程

开发新功能时：

```powershell
git switch main
git pull origin main
git switch -c feature/your-feature-name
```

开发完成后：

```powershell
git status
git add .
git commit -m "Describe your change"
git push -u origin feature/your-feature-name
```

合并回 `main`：

```powershell
git switch main
git pull origin main
git merge feature/your-feature-name
git push origin main
```

## 10. 常见问题

### 忘记当前在哪个分支

```powershell
git branch
```

带 `*` 的就是当前分支。

### 想看哪些文件被修改了

```powershell
git status
```

### 想看具体修改内容

```powershell
git diff
```

### 提交信息写错了

如果刚提交完，还没有推送，可以修改最后一次提交信息：

```powershell
git commit --amend -m "New commit message"
```

### 推送前先同步远程代码

```powershell
git pull origin main
```

如果当前在其他分支，例如 `dev`：

```powershell
git pull origin dev
```

