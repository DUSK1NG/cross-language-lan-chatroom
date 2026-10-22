import type { ReactNode } from 'react';
import { motion } from 'framer-motion';
import { Check, Plus, SlidersHorizontal } from 'lucide-react';

import catBrandMark from '../assets/lan-chat-cat.png';
import { Button } from '../components/ui/button';
import { Dialog } from '../components/ui/dialog';
import { ease } from '../lib/motion';
import { currentRelease, type ReleaseNotes, type ReleaseSectionKind } from './releaseNotes';

const sectionIcons: Record<ReleaseSectionKind, ReactNode> = {
  new: <Plus size={14} aria-hidden="true" />,
  improved: <SlidersHorizontal size={14} aria-hidden="true" />,
  fixed: <Check size={14} aria-hidden="true" />
};

type ReleaseNotesCardProps = { open?: boolean; notes?: ReleaseNotes; onClose(): void };

/** "What's new" dialog shown once per version and from 设置 › 关于. */
export function ReleaseNotesCard({ open = true, notes = currentRelease, onClose }: ReleaseNotesCardProps) {
  let index = 0;
  return (
    <Dialog open={open} onOpenChange={(next) => { if (!next) onClose(); }} title={notes.headline}
      description={`新版本 · ${notes.codename} · ${notes.date}`} className="max-w-[480px]">
      <div data-testid="release-notes-card" className="flex flex-col gap-4">
        <div className="flex items-center gap-4 rounded-md bg-bg p-4">
          <img src={catBrandMark} alt="" className="size-12 rounded-full" />
          <span className="flex min-w-0 flex-col">
            <strong className="num text-name font-semibold">LAN Chat v{notes.version}</strong>
            <span className="text-small text-ink-2">{notes.summary}</span>
          </span>
        </div>
        {notes.sections.map((section) => (
          <section key={section.kind} aria-label={section.title} className="flex flex-col gap-2">
            <h3 className="flex items-center gap-2 text-small font-medium text-accent-text">{sectionIcons[section.kind]}{section.title}</h3>
            <ul className="flex flex-col gap-2">
              {section.items.map((item) => (
                <motion.li key={item.title} data-release-item initial={{ opacity: 0, y: 6 }} animate={{ opacity: 1, y: 0 }}
                  transition={{ duration: 0.2, ease, delay: 0.12 + 0.03 * index++ }} className="flex flex-col">
                  <strong className="text-body font-medium text-ink">{item.title}</strong>
                  <span className="text-small text-ink-2">{item.detail}</span>
                </motion.li>
              ))}
            </ul>
          </section>
        ))}
        <div className="flex justify-end pt-2">
          <Button variant="primary" onClick={onClose}>知道了</Button>
        </div>
      </div>
    </Dialog>
  );
}
