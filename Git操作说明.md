# Git 推送和合并操作说明

本文说明当前文件夹 `rv1126` 如何提交到仓库、如何推送到主分支、如何推送到新分支，以及如何把分支合并回主分支。

当前仓库信息：

```powershell
# 当前本地目录
C:\Users\xinweis\OneDrive - Qualcomm\Desktop\rv1126

# 当前远程仓库
origin -> https://github.com/Ambition66/rv1126pro.git

# 当前主分支
main
```

## 1. 推送到主分支 main 的完整流程

如果你要把当前文件夹里的代码直接推送到主分支 `main`，按下面命令执行。

```powershell
# 1. 进入当前项目目录
cd "C:\Users\xinweis\OneDrive - Qualcomm\Desktop\rv1126"

# 2. 切换到主分支 main
git switch main

# 3. 拉取远程 main 最新代码，避免本地代码落后
git pull origin main

# 4. 查看当前有哪些文件被修改、新增或删除
git status

# 5. 添加当前目录下所有变更文件
git add .

# 6. 提交到本地仓库，提交说明可以按实际修改内容改
git commit -m "Update project files"

# 7. 推送到远程仓库的 main 分支
git push origin main
```

注意：

- 如果 `git commit` 提示 `nothing to commit`，说明没有需要提交的修改。
- 如果 `git pull origin main` 或 `git push origin main` 提示冲突，需要先解决冲突再继续。
- 直接推送 `main` 适合小改动；多人协作时更推荐先推分支，再合并。

## 2. 推送到新分支的完整流程

如果你不想直接推送到 `main`，可以创建一个新分支，例如 `dev`，然后把代码推送到这个分支。

```powershell
# 1. 进入当前项目目录
cd "C:\Users\xinweis\OneDrive - Qualcomm\Desktop\rv1126"

# 2. 先切换到 main 分支
git switch main

# 3. 拉取远程 main 最新代码，保证新分支基于最新 main 创建
git pull origin main

# 4. 创建并切换到新分支 dev
git switch -c dev

# 5. 查看当前有哪些文件被修改、新增或删除
git status

# 6. 添加当前目录下所有变更文件
git add .

# 7. 提交到本地 dev 分支
git commit -m "Update project files"

# 8. 把本地 dev 分支推送到远程仓库，并建立跟踪关系
git push -u origin dev
```

后续如果你已经在 `dev` 分支上继续修改代码，只需要执行：

```powershell
# 1. 确认当前在 dev 分支
git branch

# 2. 查看修改状态
git status

# 3. 添加所有变更
git add .

# 4. 提交修改
git commit -m "Update files"

# 5. 推送到远程 dev 分支
git push
```

说明：

- `dev` 是分支名，可以换成其他名字，例如 `feature/ffmpeg-update`。
- `git push -u origin dev` 只需要第一次推送新分支时使用。
- 后面这个分支已经和远程分支绑定后，直接 `git push` 就可以。

## 3. 合并分支到 main 的完整流程

假设你已经把代码推送到了 `dev` 分支，现在要把 `dev` 合并回 `main`。

```powershell
# 1. 进入当前项目目录
cd "C:\Users\xinweis\OneDrive - Qualcomm\Desktop\rv1126"

# 2. 切换到主分支 main
git switch main

# 3. 拉取远程 main 最新代码
git pull origin main

# 4. 合并 dev 分支到当前 main 分支
git merge dev

# 5. 如果合并没有冲突，把合并后的 main 推送到远程仓库
git push origin main
```

如果 `dev` 是远程分支，本地还没有这个分支，可以先执行：

```powershell
# 1. 拉取远程分支信息
git fetch origin

# 2. 创建本地 dev 分支，并关联远程 origin/dev
git switch -c dev origin/dev

# 3. 切回 main
git switch main

# 4. 拉取远程 main 最新代码
git pull origin main

# 5. 合并 dev 到 main
git merge dev

# 6. 推送合并后的 main
git push origin main
```

## 4. 合并冲突处理流程

如果执行 `git merge dev` 后出现冲突，按下面流程处理。

```powershell
# 1. 查看哪些文件冲突
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

处理方式：

- `<<<<<<< HEAD` 到 `=======` 中间是 `main` 分支的内容。
- `=======` 到 `>>>>>>> dev` 中间是 `dev` 分支的内容。
- 手动保留你需要的正确内容。
- 删除 `<<<<<<< HEAD`、`=======`、`>>>>>>> dev` 这些冲突标记。

冲突解决完成后执行：

```powershell
# 1. 添加已经解决冲突的文件
git add .

# 2. 提交合并结果
git commit -m "Resolve merge conflicts"

# 3. 推送合并后的 main
git push origin main
```

## 5. 常用检查命令

```powershell
# 查看当前分支和文件修改状态
git status

# 查看当前所在分支，本地分支前面会有 *
git branch

# 查看本地和远程所有分支
git branch -a

# 查看远程仓库地址
git remote -v

# 查看具体修改了哪些内容
git diff

# 拉取远程分支信息
git fetch origin
```

## 6. 删除已经合并的分支

如果 `dev` 已经合并到 `main`，并且后续不再需要，可以删除分支。

```powershell
# 删除本地 dev 分支
git branch -d dev

# 删除远程 dev 分支
git push origin --delete dev
```

如果本地分支还没有合并，`git branch -d dev` 会拒绝删除。确认不需要后，可以强制删除本地分支：

```powershell
# 强制删除本地 dev 分支，谨慎使用
git branch -D dev
```

## 7. 推荐使用方式

个人小修改可以直接推 `main`：

```powershell
cd "C:\Users\xinweis\OneDrive - Qualcomm\Desktop\rv1126"
git switch main
git pull origin main
git add .
git commit -m "Update project files"
git push origin main
```

多人协作或较大修改建议使用分支：

```powershell
cd "C:\Users\xinweis\OneDrive - Qualcomm\Desktop\rv1126"
git switch main
git pull origin main
git switch -c dev
git add .
git commit -m "Update project files"
git push -u origin dev
```

确认分支内容没问题后再合并：

```powershell
git switch main
git pull origin main
git merge dev
git push origin main
```

