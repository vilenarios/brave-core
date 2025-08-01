// Copyright (c) 2025 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

import * as React from 'react'
import { render, screen } from '@testing-library/react'
import InputBox, { InputBoxProps } from '.'
import { ContentType } from '../../../common/mojom'

const defaultContext: InputBoxProps['context'] = {
  conversationHistory: [],
  getScreenshots: () => {},
  async handleStopGenerating() {},
  hasAcceptedAgreement: true,
  inputText: '',
  inputTextCharCountDisplay: `0/1000`,
  isCharLimitApproaching: false,
  isCharLimitExceeded: false,
  isGenerating: false,
  isMobile: false,
  isToolsMenuOpen: false,
  pendingMessageImages: [],
  removeImage: () => {},
  resetSelectedActionType: () => {},
  selectedActionType: undefined,
  setInputText: () => {},
  setIsToolsMenuOpen: () => {},
  shouldDisableUserInput: false,
  shouldSendPageContents: true,
  submitInputTextToAPI: () => {},
  uploadImage: () => {},
  associatedContentInfo: [],
  handleVoiceRecognition: () => {},
  isUploadingFiles: false,
  disassociateContent: () => {},
  associateDefaultContent: () => {},
  getPluralString: () => Promise.resolve(''),
  tabs: [],
}

describe('input box', () => {
  it('associated content is rendered in input box before conversation starts if should send contents is true', () => {
    const { container } = render(
      <InputBox
        context={{
          ...defaultContext,
          associatedContentInfo: [{
            contentId: 1,
            contentType: ContentType.PageContent,
            contentUsedPercentage: 0.5,
            title: 'Associated Content',
            url: { url: 'https://example.com' },
            uuid: '1234'
          }],
          shouldSendPageContents: true
        }}
        conversationStarted={false}
      />
    )

    expect(screen.getByText('Associated Content', { selector: '.title'})).toBeInTheDocument()
    expect(
      container.querySelector('img[src*="//favicon2"]')
    ).toBeInTheDocument()
  })

  it('associated content is not rendered in input box before conversation starts if should send contents is false', () => {
    const { container } = render(
      <InputBox
        context={{
          ...defaultContext,
          associatedContentInfo: [{
            contentId: 1,
            contentType: ContentType.PageContent,
            contentUsedPercentage: 0.5,
            title: 'Associated Content',
            url: { url: 'https://example.com' },
            uuid: '1234'
          }],
          shouldSendPageContents: false
        }}
        conversationStarted={false}
      />
    )

    expect(screen.queryByText('Associated Content')).not.toBeInTheDocument()
    expect(
      container.querySelector('img[src*="//favicon2"]')
    ).not.toBeInTheDocument()
  })

  it('associated content is not rendered in input box when there is no associated content', () => {
    const { container } = render(
      <InputBox
        context={{
          ...defaultContext,
          associatedContentInfo: [],
          shouldSendPageContents: true
        }}
        conversationStarted={false}
      />
    )

    expect(screen.queryByText('Associated Content')).not.toBeInTheDocument()
    expect(
      container.querySelector('img[src*="//favicon2"]')
    ).not.toBeInTheDocument()
  })

  it('associated content is not rendered in input box after the conversation has started', () => {
    const { container } = render(
      <InputBox
        context={{
          ...defaultContext,
          associatedContentInfo: [{
            contentId: 1,
            contentType: ContentType.PageContent,
            contentUsedPercentage: 0.5,
            title: 'Associated Content',
            url: { url: 'https://example.com' },
            uuid: '1234'
          }],
          shouldSendPageContents: true
        }}
        conversationStarted
      />
    )

    expect(screen.queryByText('Associated Content')).not.toBeInTheDocument()
    expect(
      container.querySelector('img[src*="//favicon2"]')
    ).not.toBeInTheDocument()
  })

  it('send button is disabled when the input text is empty', () => {
    const { container } = render(
      <InputBox
        context={{
          ...defaultContext,
          inputText: ''
        }}
        conversationStarted={false}
      />
    )

    const sendButton = container.querySelector('.sendButtonDisabled')
    expect(sendButton).toBeInTheDocument()
    expect(sendButton).toHaveClass('sendButtonDisabled')
  })

  it('send button is enabled when the input text is not empty', () => {
    const { container } = render(
      <InputBox
        context={{
          ...defaultContext,
          inputText: 'test'
        }}
        conversationStarted={false}
      />
    )

    const sendButton = container.querySelector('.button')
    expect(sendButton).toBeInTheDocument()
    expect(sendButton).not.toHaveClass('sendButtonDisabled')
  })

  it('streaming button is shown while generating', () => {
    const { container } = render(
      <InputBox
        context={{ ...defaultContext, isGenerating: true }}
        conversationStarted={false}
      />
    )

    const streamingButton = container.querySelector('.streamingButton')
    expect(streamingButton).toBeInTheDocument()
    expect(streamingButton).toHaveClass('streamingButton')
  })
})
