import type { ReactNode } from 'react';

import type { MemberSummary } from '../bridge/types';
import { avatarTone } from './Avatar';
import { Icon } from './Icon';

type ProfileCardProps = { member: MemberSummary; isSelf: boolean; children?: ReactNode };

/** Discord 风格的成员资料卡：按用户色调生成的动画横幅 + 带在线光环的大头像。 */
export function ProfileCard({ member, isSelf, children }: ProfileCardProps) {
  const tone = avatarTone(member.userCode || member.displayName);
  return (
    <div className={`profile-card profile-card--tone-${tone}`} data-testid="profile-card">
      <div className="profile-card__banner" aria-hidden="true">
        <span className="profile-card__glow" />
        <span className="profile-card__dot profile-card__dot--a" />
        <span className="profile-card__dot profile-card__dot--b" />
        <span className="profile-card__dot profile-card__dot--c" />
      </div>
      <div className="profile-card__avatar-wrap">
        <span className={`profile-card__avatar avatar avatar--tone-${tone}${member.online ? ' profile-card__avatar--online' : ''}`} aria-hidden="true">
          {member.displayName.slice(0, 1).toUpperCase()}
        </span>
        <span className={`profile-card__presence${member.online ? ' profile-card__presence--online' : ''}`} aria-hidden="true" />
      </div>
      <div className="profile-card__body">
        <div className="admin-profile profile-card__identity">
          <strong>{member.displayName}#{member.userCode}</strong>
          <p>{member.admin ? '管理员' : '普通成员'} · {member.online ? '在线' : '离线'}</p>
        </div>
        <div className="profile-card__chips" aria-hidden="true">
          {member.admin && <span className="profile-card__chip profile-card__chip--admin"><Icon name="shield" size={12} />频道管理</span>}
          {isSelf && <span className="profile-card__chip"><Icon name="home" size={12} />这是你</span>}
          <span className="profile-card__chip"><Icon name="lock" size={12} />TLS 连接</span>
        </div>
        {children}
      </div>
    </div>
  );
}
