#ifndef PROFILE_H
#define PROFILE_H

/* 上传配置档：一套「GitHub 账户 + 仓库地址 + 分支 + 提交身份」的组合。
   可以存多套，切换项目时一键切档，不用每次粘 URL。

   落地在本工具目录下的 _git_upload.ini 里，键形如：
     toolbox/upload/profiles        = 名称列表（JSON 数组字符串）
     toolbox/upload/active          = 当前档名
     toolbox/upload/p/<名称>/...    = 该档的字段
   换机器拷走工具箱目录，配置连状态一起带走。

   档名列表存 JSON 而不是 QStringList：QSettings 会把 QStringList 写成逗号分隔，
   档名里带逗号就会被拆成两条（原来的遗留缺陷）。 */

#include <QString>
#include <QStringList>
#include <QVariant>

struct Profile {
    QString name;        /* 配置档名（列表里显示，也是存储键） */
    QString account;     /* GitHub 账户名（仅备注/展示；实际认证由 git 凭据管理器负责） */
    QString remote;      /* 仓库地址，如 https://github.com/user/repo.git */
    QString branch;      /* 分支名，默认 main */
    QString userName;    /* git 提交身份 user.name（留空 = 不设，用全局） */
    QString userEmail;   /* git 提交身份 user.email */
    QString folder;      /* 默认上传目录 */
    QString note;        /* 备注 */

    Profile();
    bool isValid() const { return !name.trimmed().isEmpty(); }
};

/* 配置档仓库：读写 profiles / active。 */
class ProfileStore {
public:
    ProfileStore();
    /* 指定 ini 路径（自检用临时文件，免得把真实配置写花） */
    explicit ProfileStore(const QString &iniPath);

    QStringList names() const;                 /* 全部档名，按加入顺序 */
    Profile     get(const QString &name) const;
    bool        exists(const QString &name) const;

    void save(const Profile &p);               /* 新增或覆盖 */
    void remove(const QString &name);
    bool        rename(const QString &oldName, const QString &newName);
                                                /* 源档不存在或新名已被占用时返回 false */

    QString activeName() const;                /* 当前档名；无档时为空 */
    void    setActive(const QString &name);

    Profile activeProfile() const;             /* 当前档；无档时返回空 Profile */

    /* 便捷读写单键（不加前缀，内部自动加 upload/） */
    void    setRaw(const QString &key, const QVariant &v);
    QVariant raw(const QString &key, const QVariant &def = QVariant()) const;

    QString filePath() const;

private:
    void    sanitize();   /* 开机自愈：清掉列表里的空名/重复项，修正悬空的 active */
    QString m_path;
};

#endif // PROFILE_H
